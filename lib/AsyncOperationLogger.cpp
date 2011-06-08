#include "AsyncOperationLogger.h"

using namespace qpid::store::bdb;
using namespace std;

AsyncOperationLogger::AsyncOperationLogger():
	deqStartSize(sizeof(char)+sizeof(uint64_t)+sizeof(uint64_t)),
	deqCompleteSize(sizeof(char)+sizeof(uint64_t)+sizeof(uint64_t)),
	enqCompleteSize(sizeof(char)+sizeof(uint64_t)+sizeof(uint64_t)),
	enqDataPreambleSize(sizeof(char)+sizeof(uint64_t)+sizeof(uint64_t)+sizeof(uint64_t)+sizeof(char))

{
}
AsyncOperationLogger::AsyncOperationLogger(std::string& basedir):
	enqueueFileIndex(0),
	dequeueFileIndex(0),
	logBaseDir(basedir),
	deqStartSize(sizeof(char)+sizeof(uint64_t)+sizeof(uint64_t)),
	deqCompleteSize(sizeof(char)+sizeof(uint64_t)+sizeof(uint64_t)),
	enqCompleteSize(sizeof(char)+sizeof(uint64_t)+sizeof(uint64_t)),
	enqDataPreambleSize(sizeof(char)+sizeof(uint64_t)+sizeof(uint64_t)+sizeof(uint64_t)+sizeof(char))
{
	deqStartBuff = (char*) malloc (deqStartSize);
	if (deqStartBuff==0x0)
	{
		THROW_STORE_EXCEPTION("Unable to allocate dequeue start log buffer");
	}
	deqCompleteBuff = (char*) malloc (deqCompleteSize);
	if (deqCompleteBuff==0x0) 
	{
		THROW_STORE_EXCEPTION("Unable to allocate dequeue complete log buffer");
	}
	enqRawBuff = (char*) malloc (enqCompleteSize);
	if (enqRawBuff==0x0)
	{
		THROW_STORE_EXCEPTION("Unable to allocate enqueue complete log buffer");
	}
	enqDataPreambleBuff = (char*) malloc (enqDataPreambleSize);
	if (enqDataPreambleBuff==0x0)
	{
		THROW_STORE_EXCEPTION("Unable to allocate enqueue data preamble log buffer");
	}
	enqueueFile.open(buildEnqLogName(enqueueFileIndex).c_str(),ios_base::out|ios_base::app|ios_base::binary);
	dequeueFile.open(buildDeqLogName(dequeueFileIndex).c_str(),ios_base::out|ios_base::app|ios_base::binary);
}

AsyncOperationLogger::~AsyncOperationLogger()
{
	{
		boost::mutex::scoped_lock lock(this->enqueueMutex);
		free(enqRawBuff);
		enqRawBuff=0x0;
		free(enqDataPreambleBuff);
		enqDataPreambleBuff=0x0;
		if (enqueueFile.is_open())
		{
			enqueueFile.close();
		}
	}
	{
		boost::mutex::scoped_lock lock(this->dequeueMutex);
		free(deqStartBuff);
		deqStartBuff=0x0;
		free(deqCompleteBuff);
		deqCompleteBuff=0x0;
		if (dequeueFile.is_open())
		{
			dequeueFile.close();
		}
	}
}

std::string AsyncOperationLogger::buildEnqLogName(int index,bool temp) 
{
	stringstream ss;
	ss <<logBaseDir<< "/enq";
	if (!temp) 
	{
		ss<<"0"<<index;
	} else 
	{
		ss<<"_tmp";
	}
	ss<<".log";
	return ss.str();
}

std::string AsyncOperationLogger::buildDeqLogName(int index,bool temp) 
{
	stringstream ss;
	ss <<logBaseDir<< "/deq";
	if (!temp) 
	{
		ss<<"0"<<index;
	} else 
	{
		ss<<"_tmp";
	}
	ss<<".log";
	return ss.str();
}

int AsyncOperationLogger::log_dequeue_start(uint64_t pid,uint64_t qid) 
{
	int retIndex=0;
	{
		boost::mutex::scoped_lock lock(this->dequeueMutex);
		if (log_dequeue_start_on_file(pid,qid,&this->dequeueFile,this->deqStartBuff)==0)
		{
			retIndex=this->dequeueFileIndex;
		} else
		{
			retIndex=-1;
		}
	}
	return retIndex;
	
}

int AsyncOperationLogger::log_dequeue_complete(uint64_t pid,uint64_t qid)
{
	int retIndex=0;
	{
		boost::mutex::scoped_lock lock(this->dequeueMutex);
		memset(this->deqCompleteBuff,0,this->deqCompleteSize);
		this->deqCompleteBuff[0]=AsyncOperationLogger::aolog_dequeue_complete;
		memcpy(&this->deqCompleteBuff[1],&pid,sizeof(uint64_t));
		int buffindex=1+(sizeof(uint64_t)/sizeof(char));
		memcpy(&this->deqCompleteBuff[buffindex],&qid,sizeof(uint64_t));

		try 
		{
			dequeueFile.write(this->deqCompleteBuff,this->deqCompleteSize);
			dequeueFile.flush();
			retIndex=this->dequeueFileIndex;
		} catch (...)
		{
			retIndex= -1;
		}
	}
	return retIndex;
}

int AsyncOperationLogger::log_mass_dequeue_complete(vector<PendingOperationId>& pid_list)
{
	int retIndex=0;
	int local_buff_size=this->deqCompleteSize*pid_list.size();
	char* bigbuff = (char*) malloc (local_buff_size);
	if (bigbuff)
	{
		int char_for_lluint=sizeof(uint64_t)/sizeof(char);
		int buffindex=0;
		for (unsigned int k=0;k<pid_list.size();k++)
		{
			bigbuff[buffindex]=AsyncOperationLogger::aolog_dequeue_complete;
			buffindex++;
			memcpy(&bigbuff[buffindex],&(pid_list[k].first),sizeof(uint64_t));
			buffindex+=char_for_lluint;
			memcpy(&bigbuff[buffindex],&(pid_list[k].second),sizeof(uint64_t));
			buffindex+=char_for_lluint;
		}
		{	
			boost::mutex::scoped_lock lock(this->dequeueMutex);
			try {
				dequeueFile.write(bigbuff,local_buff_size);
				dequeueFile.flush();
				retIndex=this->dequeueFileIndex;
			} catch (...)
			{
				retIndex = -1;
			}
		}
	} else 
	{
		for (unsigned int k=0;k<pid_list.size();k++)
		{
			retIndex=log_dequeue_complete(pid_list[k]);
		}
	}
	return retIndex;
}

int AsyncOperationLogger::log_mass_enqueue_complete(vector<PendingOperationId>& pid_list)
{
	int retIndex=0;
	int local_buff_size=this->enqCompleteSize*pid_list.size();
	char* bigbuff = (char*) malloc (local_buff_size);
	if (bigbuff)
	{
		int char_for_lluint=sizeof(uint64_t)/sizeof(char);
		int buffindex=0;
		for (unsigned int k=0;k<pid_list.size();k++)
		{
			bigbuff[buffindex]=AsyncOperationLogger::aolog_enqueue_complete;
			buffindex++;
			memcpy(&bigbuff[buffindex],&(pid_list[k].first),sizeof(uint64_t));
			buffindex+=char_for_lluint;
			memcpy(&bigbuff[buffindex],&(pid_list[k].second),sizeof(uint64_t));
			buffindex+=char_for_lluint;
		}
		{	
			boost::mutex::scoped_lock lock(this->enqueueMutex);
			try {
				enqueueFile.write(bigbuff,local_buff_size);
				enqueueFile.flush();
				retIndex=this->enqueueFileIndex;
			} catch (...)
			{
				retIndex = -1;
			}
		}
	} else 
	{
		for (unsigned int k=0;k<pid_list.size();k++)
		{
			retIndex=log_enqueue_complete(pid_list[k]);
		}
	}
	return retIndex;

}

bool AsyncOperationLogger::log_mass_enqueue_dequeue_complete(vector<PendingOperationId>& pid_list)
{
	bool retVal=true;
	if (log_mass_enqueue_complete(pid_list)==-1)
	{
		retVal=false;
	}
	if (log_mass_dequeue_complete(pid_list)==-1)
	{
		retVal=false;
	}
	return retVal;
}

	
int AsyncOperationLogger::log_enqueue_start(uint64_t pid,uint64_t qid,vector<char>& buff,uint64_t buffsize,bool transient)
{
	int retIndex=0;
	{
		boost::mutex::scoped_lock lock(this->enqueueMutex);
		if (log_enqueue_start_on_file(pid,qid,buff,buffsize,transient,&this->enqueueFile,this->enqDataPreambleBuff)==0)
		{
			retIndex=this->enqueueFileIndex;
		} else {
			retIndex=-1;
		}
			
	}
	return retIndex;
}

int AsyncOperationLogger::log_enqueue_complete(uint64_t pid,uint64_t qid)
{
	int retIndex=0;
	{
		boost::mutex::scoped_lock lock(this->enqueueMutex);
		memset(this->enqRawBuff,0,this->enqCompleteSize);
		this->enqRawBuff[0]= AsyncOperationLogger::aolog_enqueue_complete;
		memcpy(&this->enqRawBuff[1],&pid,sizeof(uint64_t));
		int buffindex=1+(sizeof(uint64_t)/sizeof(char));
		memcpy(&this->enqRawBuff[buffindex],&qid,sizeof(uint64_t));

		try
		{
			enqueueFile.write(this->enqRawBuff,this->enqCompleteSize);
			enqueueFile.flush();
			retIndex=this->enqueueFileIndex;
		} catch (...)
		{
			retIndex=-1;
		}
	}
	return retIndex;
}

int AsyncOperationLogger::recoverAsyncDequeue(std::set<PendingAsyncDequeue>& adset)
{
	{
		boost::mutex::scoped_lock lock(this->dequeueMutex);
		if (dequeueFile.is_open()) dequeueFile.close();
		std::set<PendingOperationId> lostSet;
		for (int k=0;k<2;k++) 
		{
			std::string logFilename = buildDeqLogName(k);
			extractPendingDequeueFromFile(adset,k,lostSet);
			remove(logFilename.c_str());
		}
		this->dequeueFileIndex=0;
		dequeueFile.open(buildDeqLogName(dequeueFileIndex).c_str(),ios_base::out|ios_base::app|ios_base::binary);
	}
	for (std::set<PendingAsyncDequeue>::iterator it=adset.begin();it!=adset.end();it++)
	{
		log_dequeue_start(it->msgId,it->queueId);
	}
	return 0;
}

int AsyncOperationLogger::recoverAsyncEnqueue(std::set<PendingAsyncEnqueue>& aeset)
{
	{
		boost::mutex::scoped_lock lock(this->enqueueMutex);
		if (enqueueFile.is_open()) enqueueFile.close();
		std::set<PendingOperationId> lostSet;	
		for (int k=0;k<2;k++) 
		{
			std::string logFilename = buildEnqLogName(k);
			extractPendingEnqueueFromFile(aeset,k,lostSet);
			remove(logFilename.c_str());
		}
		this->enqueueFileIndex=0;
		enqueueFile.open(buildEnqLogName(enqueueFileIndex).c_str(),ios_base::out|ios_base::app|ios_base::binary);
	}
	for (std::set<PendingAsyncEnqueue>::iterator it=aeset.begin();it!=aeset.end();it++)
	{
		PendingAsyncEnqueue pae = *it;
		log_enqueue_start(pae.msgId,pae.queueId,pae.buff,pae.size,pae.transient);
	}

	return 0;
}

void AsyncOperationLogger::cleanEnqueueLog(int intervalInSeconds,int warningSize)
{
	boost::posix_time::ptime last_exec=boost::posix_time::second_clock::local_time();
	char* wbuff = (char*) malloc (this->enqDataPreambleSize);
	if (wbuff == 0x0) 
	{
		THROW_STORE_EXCEPTION("Unable to allocate memory for write buffer used by the enqueue log cleaner");
	}
	while(1) 
	{
		boost::this_thread::sleep(boost::posix_time::seconds(min_clean_interval));
		boost::posix_time::time_duration dur= boost::posix_time::time_period(last_exec,boost::posix_time::second_clock::local_time()).length();
		bool notNow=false;
		if (dur.total_seconds()<intervalInSeconds)
		{
			if (this->enqueueFile.tellp()<warningSize)
			{
				notNow=true;
			} 
		} 
		if (notNow) continue;
		last_exec = boost::posix_time::second_clock::local_time();
		int handlingIndex=0;
		{
			boost::mutex::scoped_lock lock(this->enqueueMutex);
			handlingIndex=this->enqueueFileIndex;
			this->enqueueFileIndex=(this->enqueueFileIndex+1)%2;
			this->enqueueFile.close();
			enqueueFile.open(buildEnqLogName(this->enqueueFileIndex).c_str(),ios_base::out|ios_base::app|ios_base::binary);
		}
		//Now work on the unused file
		std::set<PendingOperationId> lostSet;
		std::set<PendingAsyncEnqueue> aeset;
		extractPendingEnqueueFromFile(aeset,handlingIndex,lostSet);
		std::string handlingLogname=buildEnqLogName(handlingIndex);
		std::string tmpLogName=buildEnqLogName(0,true);
		ofstream tmplog;
		tmplog.open(tmpLogName.c_str(),ios_base::out|ios_base::app|ios_base::binary);
		if (tmplog)
		{
			for (std::set<PendingAsyncEnqueue>::iterator it=aeset.begin();it!=aeset.end();it++)
			{
				PendingAsyncEnqueue pae=*it;
				log_enqueue_start_on_file(pae.msgId,pae.queueId,pae.buff,pae.size,pae.transient,&tmplog,wbuff);
			}
			tmplog.close();
			remove(handlingLogname.c_str());
			rename(tmpLogName.c_str(),handlingLogname.c_str());
		}		
		for (std::set<PendingOperationId>::iterator it = lostSet.begin();it!=lostSet.end();it++)
		{
			log_enqueue_complete(*it);
		}
	}
}

void AsyncOperationLogger::cleanDequeueLog(int intervalInSeconds,int warningSize)
{
	boost::posix_time::ptime last_exec=boost::posix_time::second_clock::local_time();
	char* wbuff = (char*) malloc (this->deqStartSize);
	if (wbuff == 0x0) 
	{
		THROW_STORE_EXCEPTION("Unable to allocate memory for write buffer used by the dequeue log cleaner");
	}
	while(1) 
	{
		boost::this_thread::sleep(boost::posix_time::seconds(min_clean_interval));
		boost::posix_time::time_duration dur= boost::posix_time::time_period(last_exec,boost::posix_time::second_clock::local_time()).length();
		bool notNow=false;
		if (dur.total_seconds()<intervalInSeconds)
		{
			if (this->dequeueFile.tellp()<warningSize)
			{
				notNow=true;
			}
		} 
		if (notNow) continue;
		last_exec = boost::posix_time::second_clock::local_time();
		int handlingIndex=0;
		{
			boost::mutex::scoped_lock lock(this->dequeueMutex);
			handlingIndex=this->dequeueFileIndex;
			this->dequeueFileIndex=(this->dequeueFileIndex+1)%2;
			this->dequeueFile.close();
			dequeueFile.open(buildDeqLogName(this->dequeueFileIndex).c_str(),ios_base::out|ios_base::app|ios_base::binary);
		}
		//Now work on the unused file
		std::set<PendingOperationId> lostSet;
		std::set<PendingAsyncDequeue> adset;
		extractPendingDequeueFromFile(adset,handlingIndex,lostSet);
		std::string handlingLogname=buildDeqLogName(handlingIndex);
		std::string tmpLogName=buildDeqLogName(0,true);
		ofstream tmplog;
		tmplog.open(tmpLogName.c_str(),ios_base::out|ios_base::app|ios_base::binary);
		if (tmplog)
		{
			for (std::set<PendingAsyncDequeue>::iterator it=adset.begin();it!=adset.end();it++)
			{
				PendingAsyncDequeue pad=*it;
				log_dequeue_start_on_file(pad.msgId,pad.queueId,&tmplog,wbuff);
			}
			tmplog.close();
			remove(handlingLogname.c_str());
			rename(tmpLogName.c_str(),handlingLogname.c_str());
		}		
		for (std::set<PendingOperationId>::iterator it = lostSet.begin();it!=lostSet.end();it++)
		{
			log_dequeue_complete(*it);
		}
	}
}

void AsyncOperationLogger::extractPendingDequeueFromFile(std::set<PendingAsyncDequeue>& adset,int fileIndex,std::set<PendingOperationId>& lostSet)
{
	char* startbuff = (char*) malloc ( this->deqStartSize);
	char* completebuff = (char*) malloc (this->deqCompleteSize);
	if ((startbuff==0x0) || (completebuff==0x0)) return;
	char opcode;
	int char_for_lluint=sizeof(uint64_t)/sizeof(char);
	std::string logFilename = buildDeqLogName(fileIndex);
	std::ifstream infile(logFilename.c_str(),ios_base::in|ios_base::binary);
	if (infile) 
	{
		do
		{
			infile.read(&opcode,sizeof(char));
			if (!infile.fail()) 
			{
				uint64_t pid;
				std::set<PendingOperationId>::iterator mapit;
				std::set<PendingAsyncDequeue>::iterator outit;
				switch (opcode)
				{
					case AsyncOperationLogger::aolog_dequeue_start:
						memset(startbuff,0,this->deqStartSize);
						infile.read(&startbuff[1],this->deqStartSize-sizeof(char));
						if (!infile.fail())
						{	
							uint64_t qid=0;
							memcpy(&pid,&startbuff[1],sizeof(uint64_t));
							int buffindex=1+char_for_lluint;						
							memcpy(&qid,&startbuff[buffindex],sizeof(uint64_t));
							PendingOperationId opid(pid,qid);
							mapit = lostSet.find(opid);
							if (mapit!=lostSet.end())
							{
								lostSet.erase(mapit);
							} else 
							{
								adset.insert(PendingAsyncDequeue(pid,qid));
							}
						}
						break;
					case AsyncOperationLogger::aolog_dequeue_complete:
						memset(completebuff,0,this->deqCompleteSize);
						infile.read(&completebuff[1],this->deqCompleteSize-sizeof(char));
						if (!infile.fail())
						{	
							uint64_t qid=0;
							memcpy(&pid,&completebuff[1],sizeof(uint64_t));
							int buffindex=1+char_for_lluint;						
							memcpy(&qid,&completebuff[buffindex],sizeof(uint64_t));
							PendingOperationId opid(pid,qid);
							outit = adset.find(opid);
							if (outit!=adset.end()) 
							{
								adset.erase(outit);
							} else
							{
								lostSet.insert(opid);
							}
						}
						break;
					default:
						break;

				}
			}
		} while (!infile.eof());
		infile.close();
	}		

}

void AsyncOperationLogger::extractPendingEnqueueFromFile(std::set<PendingAsyncEnqueue>& aeset,int fileIndex,std::set<PendingOperationId>& lostSet) 
{
	char* startbuff = (char*) malloc ( this->enqDataPreambleSize);
	char* completebuff = (char*) malloc (this->enqCompleteSize);
	if ((startbuff==0x0) || (completebuff==0x0)) return;		
	char opcode;
	int char_for_lluint=sizeof(uint64_t)/sizeof(char);
	std::string logFilename = buildEnqLogName(fileIndex);
	std::ifstream infile(logFilename.c_str(),ios_base::in|ios_base::binary);
	if (infile) 
	{
		do {
			infile.read(&opcode,sizeof(char));
			if (!infile.fail())
			{
				uint64_t pid;
				std::set<PendingOperationId>::iterator mapit;
				std::set<PendingAsyncEnqueue>::iterator outit;
				switch (opcode)
				{
					case AsyncOperationLogger::aolog_enqueue_start:
						memset(startbuff,0,this->enqDataPreambleSize);
						infile.read(&startbuff[1],this->enqDataPreambleSize-sizeof(char));
						if (!infile.fail())
						{
							uint64_t qid,size;
							bool transientFlag;
							memcpy(&pid,&startbuff[1],sizeof(uint64_t));
							int buffindex=char_for_lluint+1;
							memcpy(&qid,&startbuff[buffindex],sizeof(uint64_t));
							buffindex+=char_for_lluint;
							memcpy(&size,&startbuff[buffindex],sizeof(uint64_t));
							buffindex+=char_for_lluint;
							if (startbuff[buffindex]!=0x0)
								transientFlag=true;
							else
								transientFlag=false;
							PendingAsyncEnqueue pae(pid,qid,transientFlag);
							pae.size=size;
							pae.buff=std::vector<char>(size);
							infile.read(&pae.buff[0],size);
							if (!infile.fail()) 
							{
								PendingOperationId opid(pid,qid);
								mapit = lostSet.find(opid);
								if (mapit!=lostSet.end())
								{
									lostSet.erase(mapit);
								} else 
								{
									aeset.insert(pae);
								}
							}
						}
						break;
					case AsyncOperationLogger::aolog_enqueue_complete:
						memset(completebuff,0,this->enqCompleteSize);
						infile.read(&completebuff[1],this->enqCompleteSize-sizeof(char));
						if (!infile.fail())
						{	
							uint64_t qid;
							memcpy(&pid,&completebuff[1],sizeof(uint64_t));
							int buffindex=1+char_for_lluint;
							memcpy(&qid,&completebuff[buffindex],sizeof(uint64_t));
							PendingOperationId opid(pid,qid);
							outit = aeset.find(opid);
							if (outit!=aeset.end()) 
							{
								aeset.erase(outit);
							} else
							{
								lostSet.insert(opid);
							}
						}
						break;
					default:
						break;
				}
			}
		} while(!infile.eof());
		infile.close();

	}

}

int AsyncOperationLogger::log_enqueue_start_on_file(uint64_t pid,
						uint64_t qid,
						vector<char>& buff,
						uint64_t buffsize,
						bool transient,
						ofstream* logFile,
						char* wbuff)
{
	int retIndex=0;
	memset(wbuff,0,this->enqDataPreambleSize);
	wbuff[0]=AsyncOperationLogger::aolog_enqueue_start; //opcode
	memcpy(&wbuff[1],&pid,sizeof(uint64_t)); //Message Id
	int char_for_lluint=sizeof(uint64_t)/sizeof(char);
	int buffindex=1+char_for_lluint;
	memcpy(&wbuff[buffindex],&qid,sizeof(uint64_t));//Queue Id
	buffindex+=char_for_lluint;
	memcpy(&wbuff[buffindex],&buffsize,sizeof(uint64_t));//Message Size
	buffindex+=char_for_lluint;
	wbuff[buffindex]=(transient?0x1:0x0);
	try {
		logFile->write(wbuff,this->enqDataPreambleSize);
		logFile->write(&buff[0],buffsize);
		logFile->flush();
	} catch (...) 
	{
		retIndex=-1;
	}
	return retIndex;
}

int AsyncOperationLogger::log_dequeue_start_on_file(uint64_t pid,uint64_t qid,ofstream* logFile,char* wbuff) 
{
	int retIndex=0;
	memset(wbuff,0,this->deqStartSize);
	wbuff[0]=AsyncOperationLogger::aolog_dequeue_start;
	memcpy(&wbuff[1],&pid,sizeof(uint64_t));
	int buffindex=1+(sizeof(uint64_t)/sizeof(char));
	memcpy(&wbuff[buffindex],&qid,sizeof(uint64_t));
	try 
	{
		logFile->write(wbuff,this->deqStartSize);
		logFile->flush();
	} catch (...)
	{
		retIndex= -1;
	}
	return retIndex;
	
}


int qpid::store::bdb::operator==(const PendingAsyncOperation& left,const PendingAsyncOperation& right)
{
	return ((left.msgId==right.msgId)&&(left.queueId==right.queueId));
}
int qpid::store::bdb::operator!=(const PendingAsyncOperation& left,const PendingAsyncOperation& right)
{
	return ((left.msgId!=right.msgId)||(left.queueId!=right.queueId));
}
int qpid::store::bdb::operator<(const PendingAsyncOperation& left,const PendingAsyncOperation& right)
{
	int ret=((left.msgId<right.msgId)&&(left.queueId<right.queueId));
	if (ret) return ret;
	ret = ((left.msgId==right.msgId)&&(left.queueId<right.queueId));
	if (ret) return ret;
	ret= ((left.queueId==right.queueId)&&(left.msgId<right.msgId));
	return ret;
}
int qpid::store::bdb::operator>(const PendingAsyncOperation& left,const PendingAsyncOperation& right)
{
	int ret=((left.msgId>right.msgId)&&(left.queueId>right.queueId));
	if (ret) return ret;
	ret = ((left.msgId==right.msgId)&&(left.queueId>right.queueId));
	if (ret) return ret;
	ret= ((left.queueId==right.queueId)&&(left.msgId>right.msgId));
	return ret;
}
int qpid::store::bdb::operator<=(const PendingAsyncOperation& left,const PendingAsyncOperation& right)
{
	return ((left.msgId<=right.msgId)&&(left.queueId<=right.queueId));
}
int qpid::store::bdb::operator>=(const PendingAsyncOperation& left,const PendingAsyncOperation& right)
{
	return ((left.msgId>=right.msgId)&&(left.queueId>=right.queueId));
}

