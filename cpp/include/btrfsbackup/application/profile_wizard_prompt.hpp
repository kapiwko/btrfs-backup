#pragma once

#include <iosfwd>
#include <string>

namespace btrfsbackup::wizard {

std::string trim_text(const std::string& value);
bool parse_bool(const std::string& value);
long long parse_uint(const std::string& value);

std::string prompt_value(std::istream& input, std::ostream& output, const std::string& label, const std::string& default_value);
bool prompt_bool(std::istream& input, std::ostream& output, const std::string& label, bool default_value);
long long prompt_uint(std::istream& input, std::ostream& output, const std::string& label, long long default_value);

} // namespace btrfsbackup::wizard
