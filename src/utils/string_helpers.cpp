#include "string_helpers.hpp"

using namespace std;

namespace Util
{
	static vector<string> split(const string& str, const char* delim, bool allow_empty)
	{
		if (str.empty())
			return {};
		vector<string> ret;

		size_t start_index = 0;
		size_t index = 0;
		while ((index = str.find_first_of(delim, start_index)) != string::npos)
		{
			if (allow_empty || index > start_index)
				ret.push_back(str.substr(start_index, index - start_index));
			start_index = index + 1;

			if (allow_empty && (index == str.size() - 1))
				ret.emplace_back();
		}

		if (start_index < str.size())
			ret.push_back(str.substr(start_index));
		return ret;
	}

	vector<string> split(const string& str, const char* delim)
	{
		return split(str, delim, true);
	}

	vector<string> split_no_empty(const string& str, const char* delim)
	{
		return split(str, delim, false);
	}

	string strip_whitespace(const string& str)
	{
		string ret;
		auto index = str.find_first_not_of(" \t");
		if (index == string::npos)
			return "";
		ret = str.substr(index, string::npos);
		index = ret.find_last_not_of(" \t");
		if (index != string::npos)
			return ret.substr(0, index + 1);
		else
			return ret;
	}
}