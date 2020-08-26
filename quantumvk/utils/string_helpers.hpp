#pragma once

#include <string>
#include <sstream>
#include <vector>
#include <type_traits>

//Random string helper functions

namespace inner
{
	template<typename T>
	void join_helper(std::ostringstream& stream, T&& t)
	{
		stream << std::forward<T>(t);
	}

	template<typename T, typename... Ts>
	void join_helper(std::ostringstream& stream, T&& t, Ts&&... ts)
	{
		stream << std::forward<T>(t);
		join_helper(stream, std::forward<Ts>(ts)...);
	}
}

namespace Util
{
	template<typename... Ts>
	inline std::string join(Ts&&... ts)
	{
		std::ostringstream stream;
		inner::join_helper(stream, std::forward<Ts>(ts)...);
		return stream.str();
	}

	std::vector<std::string> split(const std::string& str, const char* delim);
	std::vector<std::string> split_no_empty(const std::string& str, const char* delim);
	std::string strip_whitespace(const std::string& str);
}
