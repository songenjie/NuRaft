/************************************************************************
Copyright 2017-2019 eBay Inc.
Author/Developer(s): Jung-Sang Ahn

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
**************************************************************************/

#include "in_memory_log_store.hxx"

#include "nuraft.hxx"

#include <cassert>

#include <fstream>
#include <iostream>

using namespace std;
namespace nuraft {

inmem_log_store::inmem_log_store()
		: start_idx_(1) {
	// Dummy entry for index 0.
	ptr<buffer> buf = buffer::alloc(sz_ulong);
	logs_[0] = cs_new<log_entry>(0, buf);
}

inmem_log_store::inmem_log_store(int my_id)
		: start_idx_(1) {
	// Dummy entry for index 0.
	ptr<buffer> buf = buffer::alloc(sz_ulong);
	logs_[0] = cs_new<log_entry>(0, buf);
	my_id_ = my_id;
	init();
}

inmem_log_store::~inmem_log_store() {}

ptr<log_entry> inmem_log_store::make_clone(const ptr<log_entry> &entry) {
	ptr<log_entry> clone = cs_new<log_entry>
			(entry->get_term(),
			 buffer::clone(entry->get_buf()),
			 entry->get_val_type());
	return clone;
}

ulong inmem_log_store::next_slot() const {
	std::lock_guard<std::mutex> l(logs_lock_);
	// Exclude the dummy entry.
	return start_idx_ + logs_.size() - 1;
}

ulong inmem_log_store::start_index() const {
	return start_idx_;
}

ptr<log_entry> inmem_log_store::last_entry() const {
	ulong next_idx = next_slot();
	std::lock_guard<std::mutex> l(logs_lock_);
	auto entry = logs_.find(next_idx - 1);
	if (entry == logs_.end()) {
		entry = logs_.find(0);
	}

	return make_clone(entry->second);
}

ulong inmem_log_store::append(ptr<log_entry> &entry) {
	ptr<log_entry> clone = make_clone(entry);

	std::lock_guard<std::mutex> l(logs_lock_);
	size_t idx = start_idx_ + logs_.size() - 1;
	logs_[idx] = clone;
	return idx;
}

void inmem_log_store::write_at(ulong index, ptr<log_entry> &entry) {
	ptr<log_entry> clone = make_clone(entry);

	// Discard all logs equal to or greater than `index.
	std::lock_guard<std::mutex> l(logs_lock_);
	auto itr = logs_.lower_bound(index);
	while (itr != logs_.end()) {
		std::cout << "write erase " << "\n";
		std::cout << "index \t " << itr->first << endl;
		std::cout << "le->get_term()  \t" << itr->second->get_term() << endl;
		std::cout << "le->get_buf() \t" << itr->second->get_buf() << endl;
		std::cout << "le->get_buf_ptr() \t" << itr->second->get_buf_ptr() << endl;
		std::cout << "le->get_val_type() \t" << itr->second->get_val_type() << endl;
		itr = logs_.erase(itr);
	}
	logs_[index] = clone;
}

ptr<std::vector<ptr<log_entry> > >
inmem_log_store::log_entries(ulong start, ulong end) {
	ptr<std::vector<ptr<log_entry> > > ret =
			cs_new<std::vector<ptr<log_entry> > >();

	ret->resize(end - start);
	ulong cc = 0;
	for (ulong ii = start; ii < end; ++ii) {
		ptr<log_entry> src = nullptr;
		{
			std::lock_guard<std::mutex> l(logs_lock_);
			auto entry = logs_.find(ii);
			if (entry == logs_.end()) {
				entry = logs_.find(0);
				assert(0);
			}
			src = entry->second;
		}
		(*ret)[cc++] = make_clone(src);
	}
	return ret;
}

ptr<std::vector<ptr<log_entry>>>
inmem_log_store::log_entries_ext(ulong start,
								 ulong end,
								 ulong batch_size_hint_in_bytes) {
	ptr<std::vector<ptr<log_entry> > > ret =
			cs_new<std::vector<ptr<log_entry> > >();

	size_t accum_size = 0;
	for (ulong ii = start; ii < end; ++ii) {
		ptr<log_entry> src = nullptr;
		{
			std::lock_guard<std::mutex> l(logs_lock_);
			auto entry = logs_.find(ii);
			if (entry == logs_.end()) {
				entry = logs_.find(0);
				assert(0);
			}
			src = entry->second;
		}
		ret->push_back(make_clone(src));
		accum_size += src->get_buf().size();
		if (batch_size_hint_in_bytes &&
			accum_size >= batch_size_hint_in_bytes)
			break;
	}
	return ret;
}

ptr<log_entry> inmem_log_store::entry_at(ulong index) {
	ptr<log_entry> src = nullptr;
	{
		std::lock_guard<std::mutex> l(logs_lock_);
		auto entry = logs_.find(index);
		if (entry == logs_.end()) {
			entry = logs_.find(0);
		}
		src = entry->second;
	}
	return make_clone(src);
}

ulong inmem_log_store::term_at(ulong index) {
	ulong term = 0;
	{
		std::lock_guard<std::mutex> l(logs_lock_);
		auto entry = logs_.find(index);
		if (entry == logs_.end()) {
			entry = logs_.find(0);
		}
		term = entry->second->get_term();
	}
	return term;
}

ptr<buffer> inmem_log_store::pack(ulong index, int32 cnt) {
	std::vector<ptr<buffer> > logs;

	size_t size_total = 0;
	for (ulong ii = index; ii < index + cnt; ++ii) {
		ptr<log_entry> le = nullptr;
		{
			std::lock_guard<std::mutex> l(logs_lock_);
			le = logs_[ii];
		}
		assert(le.get());
		ptr<buffer> buf = le->serialize();
		size_total += buf->size();
		logs.push_back(buf);
	}

	ptr<buffer> buf_out = buffer::alloc
			(sizeof(int32) +
			 cnt * sizeof(int32) +
			 size_total);
	buf_out->pos(0);
	buf_out->put((int32) cnt);

	for (auto &entry: logs) {
		ptr<buffer> &bb = entry;
		buf_out->put((int32) bb->size());
		buf_out->put(*bb);
	}
	save();
	return buf_out;
}

void inmem_log_store::save() {
	std::vector<ptr<buffer> > logs;
	size_t size_total = 0;
	int32 cnt = 0;
	std::cout << "start_index" << start_idx_ << endl;
	std::cout << "logs_.size()" << logs_.size() << endl;
	std::cout << "cnt " << cnt << endl;
	for (auto &log : logs_) {

		std::cout << "index \t " << log.first << endl;
		std::cout << "le->get_term()  \t" << log.second->get_term() << endl;
		std::cout << "le->get_buf() \t" << log.second->get_buf() << endl;
		std::cout << "le->get_buf_ptr() \t" << log.second->get_buf_ptr() << endl;
		std::cout << "le->get_val_type() \t" << log.second->get_val_type() << endl;

		if (log.first > start_idx_) {
			ptr<log_entry> le = nullptr;
			{
				std::lock_guard<std::mutex> l(logs_lock_);
				le = log.second;
			}
			assert(le.get());
			ptr<buffer> buf = le->serialize();
			size_total += buf->size();
			logs.push_back(buf);
			++cnt;
		};
	}
	std::cout << "cnt" << cnt << std::endl;
	std::cout << "size_total" << size_total << std::endl;

	if (cnt == 0) {
		return;
	}

	ptr<buffer> buf_out = buffer::alloc
			(sizeof(int32) +
			 sizeof(int32) +
			 cnt * sizeof(int32) +
			 size_total);
	buf_out->pos(0);
	buf_out->put((int32) start_idx_);
	buf_out->put((int32) cnt);

	for (auto &entry: logs) {
		ptr<buffer> &bb = entry;
		buf_out->put((int32) bb->size());
		buf_out->put(*bb);
	}
	std::cout << "write save_store start " << std::endl;
	ofstream fout("./save_store" + std::to_string(this->my_id_) + ".data");
	std::cout << "save_store size " << buf_out->size() << endl;
	std::cout << "save_store container_size " << buf_out->container_size() << endl;
	fout.write((char *) &(*buf_out), buf_out->container_size());
	fout.close();
	std::cout << "write save_store end " << std::endl;
}

void inmem_log_store::init() {
	ifstream fin("./save_store" + std::to_string(this->my_id_) + ".data");
	if (fin) {
		std::cout << "read save_store start " << std::endl;
		std::streampos fsize = 0;
		fsize = fin.tellg();
		fin.seekg(0, std::ios::end);
		fsize = fin.tellg() - fsize;
		fin.seekg(0, std::ios::beg);
		std::cout << "size ()" << std::to_string(fsize) << endl;
		std::cout << "read file size 0" << fsize << endl;
		ptr<buffer> buf2;
		if (ulong(fsize) > 0x8008) {
			buf2 = buffer::alloc(ulong(fsize) - ulong(sizeof(uint) * 2));
		} else {
			buf2 = buffer::alloc(ulong(fsize) - ulong(sizeof(ushort) * 2));
		}
		std::cout << "read file size" << buf2->container_size() << endl;

		fin.read((char *) &(*buf2), buf2->container_size());
		std::cout << "read save_config end " << std::endl;

		buffer_serializer bs(buf2);

		start_idx_ = bs.get_i32();
		int32 num_logs = bs.get_i32();

		std::cout << "start_idx_" << start_idx_ << std::endl;
		std::cout << "num_logs" << num_logs << std::endl;
		for (int32 ii = 0; ii < num_logs; ++ii) {
			ulong cur_idx = start_idx_ + ii;
			int32 buf_size = bs.get_i32();

			ptr<buffer> buf_local = buffer::alloc(buf_size);
			bs.get_buffer(buf_local);

			ptr<log_entry> le = log_entry::deserialize(*buf_local);
			{
				std::lock_guard<std::mutex> l(logs_lock_);
				logs_[cur_idx] = le;
			}
		}
		fin.close();
	}
}

void inmem_log_store::apply_pack(ulong index, buffer &pack) {
	pack.pos(0);
	int32 num_logs = pack.get_int();

	for (int32 ii = 0; ii < num_logs; ++ii) {
		ulong cur_idx = index + ii;
		int32 buf_size = pack.get_int();

		ptr<buffer> buf_local = buffer::alloc(buf_size);
		pack.get(buf_local);

		ptr<log_entry> le = log_entry::deserialize(*buf_local);
		{
			std::lock_guard<std::mutex> l(logs_lock_);
			logs_[cur_idx] = le;
		}
	}

	{
		std::lock_guard<std::mutex> l(logs_lock_);
		auto entry = logs_.upper_bound(0);
		if (entry != logs_.end()) {
			start_idx_ = entry->first;
		} else {
			start_idx_ = 1;
		}
	}
}

bool inmem_log_store::compact(ulong last_log_index) {
	std::lock_guard<std::mutex> l(logs_lock_);
	for (ulong ii = start_idx_; ii <= last_log_index; ++ii) {
		auto entry = logs_.find(ii);
		if (entry != logs_.end()) {
			std::cout << "compact erase " << "\n";
			std::cout << "index \t " << entry->first << endl;
			std::cout << "le->get_term()  \t" << entry->second->get_term() << endl;
			std::cout << "le->get_buf() \t" << entry->second->get_buf() << endl;
			std::cout << "le->get_buf_ptr() \t" << entry->second->get_buf_ptr() << endl;
			std::cout << "le->get_val_type() \t" << entry->second->get_val_type() << endl;
			logs_.erase(entry);
		}
	}

	// WARNING:
	//   Even though nothing has been erased,
	//   we should set `start_idx_` to new index.
	start_idx_ = last_log_index + 1;
	return true;
}

void inmem_log_store::close() {}

}

