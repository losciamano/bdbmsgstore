#ifndef qpid_bdbmsgstore_jcontroller_hpp
#define qpid_bdbmsgstore_jcontroller_hpp

#include "db-inc.h"
#include "jrnl/data_tok.hpp"
#include "jrnl/enums.hpp"
#include <boost/shared_ptr.hpp>

namespace qpid {
namespace bdbmsgstore {

using namespace mrg::journal;

class jcontroller {
	protected:
	/**
        * \brief Journal ID
        *
        * This string uniquely identifies this journal instance. It will most likely be associated
        * with the identity of the message queue with which it is associated.
        */
        // TODO: This is not included in any files at present, add to file_hdr?
        std::string _jid;

        /**
        * \brief Journal directory
        *
        * This string stores the path to the journal directory. It may be absolute or relative, and
        * should not end in a file separator character. (e.g. "/fastdisk/jdata" is correct,
        * "/fastdisk/jdata/" is not.)
        */
       std::string _jdir;

        /**
        * \brief Base filename
        *
        * This string contains the base filename used for the journal files. The filenames will
        * start with this base, and have various sections added to it to derive the final file names
        * that will be written to disk. No file separator characters should be included here, but
        * all other legal filename characters are valid.
        */
        std::string _base_filename;

        /**
        * \brief Initialized flag
        *
        * This flag starts out set to false, is set to true once this object has been initialized,
        * either by calling initialize() or recover().
        */
        bool _init_flag;

        /**
        * \brief Stopped flag
        *
        * This flag starts out false, and is set to true when stop() is called. At this point, the
        * journal will no longer accept messages until either initialize() or recover() is called.
        * There is no way other than through initialization to reset this flag.
        */
        // TODO: It would be helpful to distinguish between states stopping and stopped. If stop(true) is called,
        // then we are stopping, but must wait for all outstanding aios to return before being finally stopped. During
        // this period, however, no new enqueue/dequeue/read requests may be accepted.
        bool _stop_flag;

        /**
        * \brief Read-only state flag used during recover.
        *
        * When true, this flag prevents journal write operations (enqueue and dequeue), but
        * allows read to occur. It is used during recovery, and is reset when recovered() is
        * called.
        */
        bool _readonly_flag;

        /**
        * \brief If set, calls stop() if the jouranl write pointer overruns dequeue low water
        *     marker. If not set, then attempts to write will throw exceptions until the journal
        *     file low water marker moves to the next journal file.
        */
        bool _autostop;             ///< Autostop flag - stops journal when overrun occurs

	boost::shared_ptr<DbEnv> dbenv;
	public:
	/**
	* \brief Journal constructor.
	*
	* Constructor which sets the physical file location and base name.
	*
	* \param jid A unique identifier for this journal instance.
	* \param jdir The directory which will contain the journal files.
	* \param base_filename The string which will be used to start all journal filenames.
	*/
	jcontroller(const std::string& jid, const std::string& jdir, const std::string& base_filename);

	/**
	* \brief Destructor.
	*/
	virtual ~jcontroller();
        inline const std::string& id() const { return _jid; }
        inline const std::string& jrnl_dir() const { return _jdir; }
	void initialize();
	void recover();
	void recover_complete();
	void delete_jrnl_files();
	int enqueue_data_record(const void* const data_buff, const std::size_t tot_data_len,
                const std::size_t this_data_len, data_tok* dtokp, const bool transient = false);

        int enqueue_extern_data_record(const std::size_t tot_data_len, data_tok* dtokp,
                const bool transient = false);
	int enqueue_txn_data_record(const void* const data_buff, const std::size_t tot_data_len,
                const std::size_t this_data_len, data_tok* dtokp, const std::string& xid,
                const bool transient = false);
        int enqueue_extern_txn_data_record(const std::size_t tot_data_len, data_tok* dtokp,
                const std::string& xid, const bool transient = false);
	
        int dequeue_data_record(data_tok* const dtokp, const bool txn_coml_commit = false);

        int dequeue_txn_data_record(data_tok* const dtokp, const std::string& xid, const bool txn_coml_commit = false);
	
        int txn_abort(data_tok* const dtokp, const std::string& xid);

        bool is_txn_synced(const std::string& xid);

        /**
        * \brief Check if the journal is stopped.
        *
        * \return <b><i>true</i></b> if the jouranl is stopped;
        *     <b><i>false</i></b> otherwise.
        */
        inline bool is_stopped() { return _stop_flag; }

        /**
        * \brief Check if the journal is ready to read and write data.
        *
        * Checks if the journal is ready to read and write data. This function will return
        * <b><i>true</i></b> if the journal has been either initialized or restored, and the stop()
        * function has not been called since the initialization.
        *
        * Note that the journal may also be stopped if an internal error occurs (such as running out
        * of data journal file space).
        *
        * \return <b><i>true</i></b> if the journal is ready to read and write data;
        *     <b><i>false</i></b> otherwise.
        */
        inline bool is_ready() const { return _init_flag && !_stop_flag; }

        inline bool is_read_only() const { return _readonly_flag; }

        /**
        * \brief Get the journal directory.
        *
        * This returns the journal directory as set during initialization. This is the directory
        * into which the journal files will be written.
        */
        inline const std::string& dirname() const { return _jdir; }

        /**
        * \brief Get the journal base filename.
        *
        * Get the journal base filename as set during initialization. This is the prefix used in all
        * journal files of this instance. Note that if more than one instance of the journal shares
        * the same directory, their base filenames <b>MUST</b> be different or else the instances
        * will overwrite one another.
        */
        inline const std::string& base_filename() const { return _base_filename; }

	void stop(const bool block_till_aio_cmpl = false);

        /**
        * \brief Force a flush of the write page cache, creating a single AIO write operation.
        */
        int flush(const bool block_till_aio_cmpl = false);

	
	// Logging
        virtual void log(log_level level, const std::string& log_stmt) const;
        virtual void log(log_level level, const char* const log_stmt) const;

	protected:
  	static bool _init;
        static bool init_statics();



};
} //~namespace bdbmsgstore
} //~namespace qpid
#endif //~qpid_bdbmsgstore_jcontroller_hpp
