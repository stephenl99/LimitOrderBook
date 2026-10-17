#include "Core/Decoder/ItchMutexDecoder.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

#include "Core/MappedFile.h"
#include "Message/Itch/DecodeMessage.h"
#include "Message/Itch/FrameReader.h"
#include "Message/Itch/HandleMessage.h"

std::expected<std::unique_ptr<ItchBook>, std::string> ItchMutexDecoder::decode_file(const std::filesystem::path& input_path)
{
    const auto file = MappedFile::open(input_path);
    if (!file) {
        return std::unexpected("decode_file: " + file.error());
    }

    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    auto book = std::make_unique<ItchBook>();
    std::queue<std::unique_ptr<itch::Message>> q;

    auto parse_function = [&] {
        itch::FrameReader frames(file->bytes());
        while (const auto frame = frames.next()) {
            auto message = itch::decode_message(*frame);
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
