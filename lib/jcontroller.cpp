#include "jcontroller.hpp"
#include "jrnl/jexception.hpp"

namespace qpid {
namespace bdbmsgstore {

jcontroller::jcontroller(const std::string& jid, const std::string& jdir, const std::string& base_filename):
    _jid(jid),
    _jdir(jdir),
    _base_filename(base_filename),
    _init_flag(false),
    _stop_flag(false),
    _readonly_flag(false)
{
}


jcontroller::~jcontroller()
{
    if (_init_flag && !_stop_flag)
        try { stop(true); }
        catch (const jexception& e) { std::cerr << e << std::endl; }
}

void jcontroller::initialize() {
	_init_flag = false;
	_stop_flag = false;
	_readonly_flag = false;
	dbenv.reset(new DbEnv(0));
	dbenv->set_errpfx("bdbmsgstore");
	dbenv->set_lg_regionmax(256000); // default = 65000
	dbenv->open(jrnl_dir().c_str(),
		DB_THREAD | DB_CREATE | DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG | DB_INIT_MPOOL | DB_USE_ENVIRON | DB_RECOVER, 0);
	_init_flag = true;
}

void jcontroller::recover() {
	//TODO
}

void jcontroller::recover_complete() {
	//TODO
}

void jcontroller::delete_jrnl_files() {
	//TODO
}

int jcontroller::enqueue_data_record(const void* const data_buff, const std::size_t tot_data_len,
                const std::size_t this_data_len, data_tok* dtokp, const bool transient){
	//TODO
}

int jcontroller::enqueue_extern_data_record(const std::size_t tot_data_len, data_tok* dtokp,
                const bool transient) {
	//TODO
}

int jcontroller::enqueue_txn_data_record(const void* const data_buff, const std::size_t tot_data_len,
                const std::size_t this_data_len, data_tok* dtokp, const std::string& xid,
                const bool transient) {
	//TODO
}
int jcontroller::enqueue_extern_txn_data_record(const std::size_t tot_data_len, data_tok* dtokp,
                const std::string& xid, const bool transient) {
	//TODO
}

int jcontroller::dequeue_data_record(data_tok* const dtokp, const bool txn_coml_commit) {
	//TODO
}

int jcontroller::dequeue_txn_data_record(data_tok* const dtokp, const std::string& xid, const bool txn_coml_commit) {
	//TODO
}

void jcontroller::stop(const bool block_till_aio_cmpl) {
	//TODO
}
int jcontroller::flush(const bool block_till_aio_cmpl){
	//TODO
}

void jcontroller::log(log_level ll, const std::string& log_stmt) const
{
    log(ll, log_stmt.c_str());
}

void jcontroller::log(log_level ll, const char* const log_stmt) const
{
    if (ll > LOG_INFO)
    {
        std::cout << log_level_str(ll) << ": Journal \"" << _jid << "\": " << log_stmt << std::endl;
    }
}

}} //~namespace qpid::bdbmsgstore
