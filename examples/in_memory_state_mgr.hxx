#pragma once

#include <iostream>
#include <fstream>

#include "in_memory_log_store.hxx"
#include "src/tracer.hxx"
#include "nuraft.hxx"

using namespace std;
namespace nuraft {

class inmem_state_mgr : public state_mgr {
public:
	inmem_state_mgr(int srv_id,
					const std::string &endpoint)
			: my_id_(srv_id), my_endpoint_(endpoint), cur_log_store_(cs_new<inmem_log_store>(srv_id)) {

		std::cout << "read save_config start " << std::endl;
		ifstream fin("./save_config" + std::to_string(this->my_id_) + ".data");

		if (fin) {
			std::streampos fsize = 0;
			fsize = fin.tellg();
			fin.seekg(0, std::ios::end);
			fsize = fin.tellg() - fsize;
			fin.seekg(0, std::ios::beg);
			std::cout << "size ()" << std::to_string(fsize) << endl;
			ptr<buffer> buf2;
			if (ulong(fsize) > 0x8008) {
				buf2 = buffer::alloc(ulong(fsize) - ulong(sizeof(uint) * 2));
			} else {
				buf2 = buffer::alloc(ulong(fsize) - ulong(sizeof(ushort) * 2));
			}
			fin.read((char *) &(*buf2), buf2->container_size());
			fin.close();
			std::cout << "read save_config end " << std::endl;
			saved_config_ = cluster_config::deserialize(*buf2);


			bool ifexists = false;
			for (auto &entry: saved_config_->get_servers()) {
				const ptr<srv_config> &srv = entry;
				std::cout
						<< "get_aux \t " << srv->get_aux() << "\n"
						<< "get_dc_id \t " << srv->get_dc_id() << "\n"
						<< "get_endpoint \t " << srv->get_endpoint() << "\n"
						<< "get_id \t " << srv->get_id() << "\n"
						<< "get_priority \t " << srv->get_priority() << "\n";

				if (srv->get_id() == srv_id) {
					ifexists = true;
					my_srv_config_ = srv;
				}
			}
			if (!ifexists) {
				my_srv_config_ = cs_new<srv_config>(srv_id, endpoint);
				saved_config_->get_servers().push_back(my_srv_config_);
			}
		} else {
			my_srv_config_ = cs_new<srv_config>(srv_id, endpoint);
			// Initial cluster config: contains only one server (myself).
			saved_config_ = cs_new<cluster_config>();
			saved_config_->get_servers().push_back(my_srv_config_);
		}
		std::cout
				<< "get_log_idx \t " << saved_config_->get_log_idx() << "\n"
				<< "get_prev_log_idx \t " << saved_config_->get_prev_log_idx() << "\n"
				<< "get_user_ctx \t " << saved_config_->get_user_ctx() << "\n";

		ifstream finsave_state("./save_state" + std::to_string(this->my_id_) + ".data");
		if (finsave_state) {
			std::cout << "read save_state start " << std::endl;
			ptr<buffer> buf3 = buffer::alloc(sizeof(uint8_t) +
											 sizeof(uint64_t) +
											 sizeof(int32_t) +
											 sizeof(uint8_t));
			finsave_state >> *buf3;
			finsave_state.close();
			std::cout << "read save_state end " << std::endl;

			saved_state_ = srv_state::deserialize(*buf3);
			std::cout
					<< "get_term \t " << saved_state_->get_term() << "\n"
					<< "get_voted_for \t " << saved_state_->get_voted_for() << "\n";
		}
		std::cout << "read save_state start 2" << std::endl;

	}

	~inmem_state_mgr() {}

	ptr<cluster_config> load_config() {
		// Just return in-memory data in this example.
		// May require reading from disk here, if it has been written to disk.
		return saved_config_;
	}

	void save_config(const cluster_config &config) {
		ptr<buffer> buf = config.serialize();
		std::cout << "write save_config start " << std::endl;
		ofstream fout("./save_config" + std::to_string(this->my_id_) + ".data");
		fout.write((char *) &(*buf), buf->container_size());
		fout.close();
		std::cout << "write save_config end " << std::endl;
		saved_config_ = cluster_config::deserialize(*buf);
		for (auto &entry: saved_config_->get_servers()) {
			const ptr<srv_config> &srv = entry;
			std::cout
					<< "get_aux \t " << srv->get_aux() << "\n"
					<< "get_dc_id \t " << srv->get_dc_id() << "\n"
					<< "get_endpoint \t " << srv->get_endpoint() << "\n"
					<< "get_id \t " << srv->get_id() << "\n"
					<< "get_priority \t " << srv->get_priority() << "\n";
		}
		std::cout
				<< "get_log_idx \t " << saved_config_->get_log_idx() << "\n"
				<< "get_prev_log_idx \t " << saved_config_->get_prev_log_idx() << "\n"
				<< "get_user_ctx \t " << saved_config_->get_user_ctx() << "\n";
	}

	void save_state(const srv_state &state) {
		// Just keep in memory in this example.
		// Need to write to disk here, if want to make it durable.
		ptr<buffer> buf = state.serialize();
		std::cout << "write save_state start " << std::endl;
		ofstream fout("./save_state" + std::to_string(this->my_id_) + ".data");
		fout << *buf;
		fout.close();
		std::cout << "write save_state end " << std::endl;

		saved_state_ = srv_state::deserialize(*buf);
		std::cout
				<< "get_term \t " << saved_state_->get_term() << "\n"
				<< "get_voted_for \t " << saved_state_->get_voted_for() << "\n";

	}

	ptr<srv_state> read_state() {
		// Just return in-memory data in this example.
		// May require reading from disk here, if it has been written to disk.
		return saved_state_;
	}

	ptr<log_store> load_log_store() {
		return cur_log_store_;
	}

	int32 server_id() {
		return my_id_;
	}

	void system_exit(const int exit_code) {
	}

	ptr<srv_config> get_srv_config() const { return my_srv_config_; }

private:
	int my_id_;
	std::string my_endpoint_;
	ptr<inmem_log_store> cur_log_store_;
	ptr<srv_config> my_srv_config_;
	ptr<cluster_config> saved_config_;
	ptr<srv_state> saved_state_;
};

};