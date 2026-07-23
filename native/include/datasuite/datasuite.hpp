#ifndef DATASUITE_EXPORT_HPP
#define DATASUITE_EXPORT_HPP

#include <array>
#include <string_view>

// 1. MACROS FOR COMPILATION CONTROL
// These must remain macros as they handle compiler-specific visibility attributes.
#ifdef _WIN32
	#ifdef DATASUITE_STATIC_LIB
		#define DATASUITE_API
	#else
		#ifdef DATASUITE_EXPORTS
			#define DATASUITE_API __declspec(dllexport)
		#else
			#define DATASUITE_API __declspec(dllimport)
		#endif
	#endif
#else
	#define DATASUITE_API __attribute__((visibility("default")))
#endif

namespace datasuite {

	namespace version {
		// Project version
		inline constexpr int major = 6;
		inline constexpr int minor = 0;
		inline constexpr int patch = 5;

		// Binary version
		// See: https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html
		inline constexpr int binary_current = 14;
		inline constexpr int binary_revision = 5;
		inline constexpr int binary_age = 0;

		// Kernel protocol version (Single Source of Truth)
		inline constexpr int kernel_protocol_major = 5;
		inline constexpr int kernel_protocol_minor = 6;

        // Compile-time string generator to replace DATASUITE_CONCATENATE and DATASUITE_STRINGIFY
        consteval auto makeVersionString() {
            std::array<char, 16> buf{};
            int index = 0;

            // Helper lambda to append an integer to our character array
            auto append_int = [&](int num) {
                if (num == 0) {
                    buf[index++] = '0';
                    return;
                }
                int start = index;
                while (num > 0) {
                    buf[index++] = '0' + (num % 10);
                    num /= 10;
                }
                // Reverse the inserted digits
                for (int i = start, j = index - 1; i < j; ++i, --j) {
                    char temp = buf[i];
                    buf[i] = buf[j];
                    buf[j] = temp;
                }
                };

            // Construct "Major.Minor"
            append_int(kernel_protocol_major);
            buf[index++] = '.';
            append_int(kernel_protocol_minor);
            buf[index] = '\0'; // Null terminator

            return buf;
        }

        // Evaluate the array into static storage at compile time
        inline constexpr auto kernel_protocol_version_arr = makeVersionString();

        // Expose as a clean, globally readable string_view
        inline constexpr std::string_view kernel_protocol_version{ kernel_protocol_version_arr.data() };
	}

}


#endif // DATASUITE_EXPORT_HPP
