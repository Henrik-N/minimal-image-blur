// Clang 14.0.0-1ubuntu1.1
// clang++ -o blur main.cpp -std=c++20 -g -O3

#include <cmath>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>


#define checkm(x, m) { if (!(x)) { std::cerr << "error at line " << __LINE__ << " " << m << std::endl; std::exit(EXIT_FAILURE); }}
#define check(x) checkm(x, "")

namespace fs = std::filesystem;

static std::tuple<fs::path, fs::path, float> parse_args(const int argc, char* argv[]);

int32_t main(int32_t argc, char* argv[])
{
    static_assert(sizeof(char) == sizeof(uint8_t));

    const auto /* [fs::path, fs::path], float */ [input_file_path, output_file_path, blur_factor] = parse_args(argc, argv);

    std::vector<uint8_t> data;
    {
        std::FILE* file = std::fopen(input_file_path.c_str(), "rb");
        checkm(file, "failed to open " << input_file_path);

        data.resize(fs::file_size(input_file_path));
        check(std::fread(data.data(), sizeof(uint8_t), data.size(), file) == data.size());

        std::fclose(file); 
    }

    constexpr size_t header_size = 18;
    const size_t image_id_size = static_cast<size_t>(data[0]);
    const uint8_t image_type = data[sizeof(uint8_t) * 2];
    const uint16_t image_width = *reinterpret_cast<uint16_t*>(&data[sizeof(uint8_t) * 4 + sizeof(uint16_t) * 4]);
    const uint16_t image_height = *reinterpret_cast<uint16_t*>(&data[sizeof(uint8_t) * 4 + sizeof(uint16_t) * 5]);
    const uint8_t bits_per_pixel = data[sizeof(uint8_t) * 4 + sizeof(uint16_t) * 6];

    checkm(image_type == 2 || image_type == 3, "only uncompressed images are supported");

    const size_t num_pixels = image_height * image_width;
    const size_t bytes_per_pixel = bits_per_pixel / 8;
    const size_t image_data_offset = header_size + image_id_size;
    const size_t image_data_size = num_pixels * bytes_per_pixel;

    std::span<uint8_t> src = {&data[image_data_offset], image_data_size};

    std::vector<uint8_t> dst;
    dst.resize(src.size());


    const size_t blur_range = static_cast<size_t>(blur_factor * 150.f);
    {
        double row_total[4] = {0, 0, 0, 0};
        const size_t row_length = (blur_range * 2 + 1);

        auto get_buffer_offset = [bytes_per_pixel](const size_t pixel_idx, const size_t channel_idx) -> size_t
        {
            return (pixel_idx * bytes_per_pixel) + channel_idx;
        };

        for (size_t px_idx = 0; px_idx < row_length; ++px_idx)
        {
            for (size_t ch_idx = 0; ch_idx < bytes_per_pixel; ++ch_idx)
            {
                const size_t buffer_offset = get_buffer_offset(px_idx, ch_idx);
                row_total[ch_idx] += src[buffer_offset];
            }
        }

        for (size_t px_idx = blur_range; px_idx < (image_width * image_height) - blur_range; ++px_idx)
        {
            // for each color byte, set the resulting average
            for (size_t ch_idx = 0; ch_idx < bytes_per_pixel; ++ch_idx) // channel index
            {
                const size_t buffer_offset = get_buffer_offset(px_idx, ch_idx);

                dst[buffer_offset] = static_cast<uint8_t>(std::round(row_total[ch_idx] / static_cast<float>(row_length)));

                row_total[ch_idx] -= src[get_buffer_offset(px_idx - blur_range, ch_idx)];
                row_total[ch_idx] += src[get_buffer_offset(px_idx + blur_range + 1, ch_idx)];
            }
        }
    }

    {
        std::FILE* file = std::fopen(output_file_path.c_str(), "wb");
        checkm(file, "failed to open " << output_file_path);

        check(std::fwrite(data.data(), sizeof(uint8_t), data.size(), file) == data.size());

        std::fclose(file);
    }



    return EXIT_SUCCESS;
}

static void print_usage()
{
    std::cout << "\nUsage:\tblur <src_image> <dst_image> <blur_factor>\n";
    std::cout << "\t<src_image>\tpath to tga image to blur\n";
    std::cout << "\t<dst_image>\tpath to write blurred tga image to (optional, src_image_blurred if left out)\n";
    std::cout << "\t<blur_factor>\tpoint value between 0.0 and 1.0 denoting blur intensity\n";
    std::cout << std::flush;
}

std::tuple<fs::path, fs::path, float> parse_args(const int32_t argc, char* argv[])
{
    // If the path is relative, returns the full system path.
    auto to_full_path = [](const fs::path& path) -> fs::path
    {
        if (!path.is_relative())
        {
            return path;
        }
        return fs::current_path() / path.parent_path() / path.filename();
    };

    // Replaces '.filetype' at the end of the filename with param new_ext.
    auto replace_file_extension = [](const fs::path& path, const std::string_view new_ext) -> fs::path
    {
        return path.parent_path() / fs::path{std::string{path.stem()} + new_ext.data()};
    };

    if (argc < 3)
    {
        print_usage();
        std::exit(EXIT_SUCCESS);
    }

    const fs::path input_file_path = to_full_path(fs::path{argv[1]});
    checkm(fs::exists(input_file_path), "no file at " << input_file_path);

    const bool has_output_path_arg = argc >= 4; // has 3 args passed to program

    fs::path output_file_path;
    if (has_output_path_arg)
    {
        output_file_path = to_full_path(fs::path{argv[2]});
    }
    else
    {
        if (!input_file_path.has_stem())
        {
            print_usage();
            std::exit(EXIT_SUCCESS);
        }

        output_file_path = replace_file_extension(input_file_path, "_blurred.tga");
    }

    const size_t blur_factor_arg_idx = has_output_path_arg ? 3 : 2;

    char* end;
    const float blur_factor = std::strtof(argv[blur_factor_arg_idx], &end);
    if (!end || *end != 0 || blur_factor < 0.f || blur_factor > 1.f)
    {
        std::cout << "Invalid blur factor format. Should be floating point value between 0 and 1." << std::endl;
        print_usage();
        std::exit(EXIT_SUCCESS);
    }


    // Avoid overriding existing files the user might want to keep
#if 0
    if (fs::exists(output_file_path))
    {
        std::cout << "A file already exists at " << output_file_path << ". Would you like to override it? (y/N)" << std::endl;

        std::string line;
        if (!std::getline(std::cin, line))
        {
            std::cout << "std::getline failed. Are you running in a console that doesn't support writing to stdin?" << std::endl;
        }

        if (const bool is_okay = !line.empty() && 
                                  (line[0] == 'y' || line[0] == 'Y');
            !is_okay)
        {
            std::exit(EXIT_SUCCESS);
        }
    }
#endif

    return std::make_tuple(input_file_path, output_file_path, blur_factor);
}

