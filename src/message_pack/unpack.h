#ifndef UNPACK_H
#define UNPACK_H

#include "unistd.h"
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <typeinfo>
#include "endian.h"
#include "type_codes.h"
#include "../to_string.h"
#include "../backtrace.h"

struct unpacker_error: public std::runtime_error {
	unpacker_error(const std::string &error): runtime_error(error) {}
};

template <typename Stream>
class Unpacker {
public:
	inline Unpacker(Stream &stream): stream(stream) {}

	// reads the next value of the selected type from the data stream, detecting the encoding format and converting
	// to the type, applying byte order conversion if necessary.
	template <typename T>
	T next() {
		T value;
		*this >> value;
		return value;
	}

	size_t next_array_length() {
		uint8_t leader = read_bytes<uint8_t>();

		if (leader >= MSGPACK_FIXARRAY_MIN && leader <= MSGPACK_FIXARRAY_MAX) {
			return (leader & 15);
		}

		switch (leader) {
			case MSGPACK_ARRAY16:
				return ntohs(read_bytes<uint16_t>());

			case MSGPACK_ARRAY32:
				return ntohl(read_bytes<uint32_t>());

			default:
				backtrace();
				throw unpacker_error("Don't know how to convert MessagePack type " + to_string((int)leader) + " to array");
		}
	}

	size_t next_map_length() {
		uint8_t leader = read_bytes<uint8_t>();

		if (leader >= MSGPACK_FIXMAP_MIN && leader <= MSGPACK_FIXMAP_MAX) {
			return (leader & 15);
		}

		switch (leader) {
			case MSGPACK_MAP16:
				return ntohs(read_bytes<uint16_t>());

			case MSGPACK_MAP32:
				return ntohl(read_bytes<uint32_t>());

			default:
				backtrace();
				throw unpacker_error("Don't know how to convert MessagePack type " + to_string((int)leader) + " to map");
		}
	}


	// reads the given type as bytes from the data stream, without byte order conversion or type unmarshalling
	template <typename T>
	T read_bytes() {
		T obj;
		read_bytes((uint8_t *)&obj, sizeof(obj));
		return obj;
	}

	// reads the given number of bytes from the data stream, without byte order conversion or type unmarshalling
	inline void read_bytes(uint8_t *buf, size_t bytes) {
		stream.read(buf, bytes);
	}

protected:
	Stream &stream;
};

template <typename Stream, typename T>
Unpacker<Stream> &operator >>(Unpacker<Stream> &unpacker, T &obj) {
	uint8_t leader = unpacker.template read_bytes<uint8_t>();

	if (leader >= MSGPACK_POSITIVE_FIXNUM_MIN && leader <= MSGPACK_POSITIVE_FIXNUM_MAX) {
		obj = (T) leader;

	} else if (leader >= MSGPACK_NEGATIVE_FIXNUM_MIN && leader <= MSGPACK_NEGATIVE_FIXNUM_MAX) {
		obj = (T) (int8_t)leader;

	} else {
		switch (leader) {
			case MSGPACK_FALSE:
				obj = (T) false;
				break;

			case MSGPACK_TRUE:
				obj = (T) true;
				break;

			case MSGPACK_FLOAT:
				obj = (T) unpacker.template read_bytes<float>();
				break;

			case MSGPACK_DOUBLE:
				obj = (T) unpacker.template read_bytes<double>();
				break;

			case MSGPACK_UINT8:
				obj = (T) unpacker.template read_bytes<uint8_t>();
				break;

			case MSGPACK_UINT16:
				obj = (T) ntohs(unpacker.template read_bytes<uint16_t>());
				break;

			case MSGPACK_UINT32:
				obj = (T) ntohl(unpacker.template read_bytes<uint32_t>());
				break;

			case MSGPACK_UINT64:
				obj = (T) ntohll(unpacker.template read_bytes<uint64_t>());
				break;

			case MSGPACK_INT8:
				obj = (T) unpacker.template read_bytes<int8_t>();
				break;

			case MSGPACK_INT16:
				obj = (T) ntohs(unpacker.template read_bytes<int16_t>());
				break;

			case MSGPACK_INT32:
				obj = (T) ntohl(unpacker.template read_bytes<int32_t>());
				break;

			case MSGPACK_INT64:
				obj = (T) ntohll(unpacker.template read_bytes<int64_t>());
				break;

			default:
				backtrace();
				throw unpacker_error("Don't know how to convert MessagePack type " + to_string((int)leader) + " to type " + typeid(T).name());
		}
	}
	return unpacker;
}

template <typename Stream>
Unpacker<Stream> &operator >>(Unpacker<Stream> &unpacker, std::string &obj) {
	uint8_t leader = unpacker.template read_bytes<uint8_t>();

	if (leader >= MSGPACK_FIXRAW_MIN && leader <= MSGPACK_FIXRAW_MAX) {
		obj.resize(leader & 31);
	} else {
		switch(leader) {
			case MSGPACK_RAW16:
				obj.resize(ntohs(unpacker.template read_bytes<uint16_t>()));
				break;

			case MSGPACK_RAW32:
				obj.resize(ntohl(unpacker.template read_bytes<uint32_t>()));
				break;

			default:
				backtrace();
				throw unpacker_error("Don't know how to convert MessagePack type " + to_string((int)leader) + " to string");
		}
	}

	unpacker.read_bytes((uint8_t *)obj.data(), obj.size());
	return unpacker;
}

template <typename Stream, typename T>
Unpacker<Stream> &operator >>(Unpacker<Stream> &unpacker, std::vector<T> &obj) {
	size_t array_length = unpacker.next_array_length();
	obj.clear();
	obj.reserve(array_length);
	while (array_length--) {
		obj.push_back(unpacker.template next<T>());
	}
	return unpacker;
}

template <typename Stream, typename K, typename V>
Unpacker<Stream> &operator >>(Unpacker<Stream> &unpacker, std::map<K, V> &obj) {
	size_t map_length = unpacker.next_map_length();
	obj.clear();
	obj.reserve(map_length);
	while (map_length--) {
		K key = unpacker.template next<K>();
		V val = unpacker.template next<V>();
		obj[key] = val;
	}
	return unpacker;
}

#endif
