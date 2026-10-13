#include "Baseline/MutexDecoder.h"

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>

#include "Itch/DecodeMessage.h"
#include "Itch/HandleMessage.h"

std::expected<std::unique_ptr<Book>, std::string> MutexDecoder::decode_file(const std::filesystem::path& input_path)
{
    std::ifstream input_stream(input_path, std::ios::binary);
    if (!input_stream) {
        return std::unexpected(std::string("decode_file: cannot open ") + input_path.string());
    }

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    auto book = std::make_unique<Book>();
    std::queue<std::unique_ptr<itch::Message>> q;

    auto parse_function = [&] {
        while (true) {
            uint8_t len_buf[2]{};
            input_stream.read(reinterpret_cast<char*>(len_buf), 2);
            if (input_stream.gcount() != 2) {
                break;
            }

            const auto len = static_cast<uint16_t>((len_buf[0] << 8) | len_buf[1]);
            if (len == 0) {
                break;
            }

            std::vector<uint8_t> data(len);
            input_stream.read(reinterpret_cast<char*>(data.data()), len);
            if (input_stream.gcount() != static_cast<std::streamsize>(len)) {
                break;
            }

            auto message = itch::decode_message(data);
            {
                std::lock_guard lock(mtx);
                if (message != nullptr) {
                    q.push(std::move(message));
                }
            }
            cv.notify_one();
        }

        {
            std::lock_guard lock(mtx);
            done = true;
        }
        cv.notify_all();
    };

    auto handle_function = [&] {
        while (true) {
            std::unique_ptr<itch::Message> message;
            {
                std::unique_lock lock(mtx);
                cv.wait(lock, [&] { return !q.empty() || done; });
                if (q.empty() && done) {
                    break;
                }
                message = std::move(q.front());
                q.pop();
            }
            itch::handle_message(message, book.get());
        }
    };

    std::thread parse_thread(parse_function);
    std::thread action_thread(handle_function);
    parse_thread.join();
    action_thread.join();
    return book;
}
