#ifndef DATASUITE_SYSTEM_HPP
#define DATASUITE_SYSTEM_HPP

#include <string>

#include "datasuite.hpp"

namespace datasuite 
{
	DATASUITE_API
	std::string get_temp_directory_path();

    DATASUITE_API
    bool create_directory(const std::string& path);

    DATASUITE_API
    int get_current_pid();

    DATASUITE_API
    std::size_t get_tmp_hash_seed();

    DATASUITE_API
    std::string get_tmp_prefix(const std::string& process_name);

    DATASUITE_API
    std::string executable_path();

    DATASUITE_API
    std::string prefix_path();
}

#endif // !DATASUITE_SYSTEM_HPP
