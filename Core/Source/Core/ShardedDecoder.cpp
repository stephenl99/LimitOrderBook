#include "ShardedDecoder.h"

#include <thread>
#include <vector>

#include "MappedFile.h"
#include "MessagePipeline.h"
#include "Message/Itch/DecodeMessage.h"
#include "Message/Itch/FrameReader.h"
#include "Message/Itch/HandleMessage.h"

namespace {

constexpr std::size_t kQueueCapacity = 1 << 16;

struct ApplyToBook {
    Book* book;
    void operator()(std::unique_ptr<itch::Message>& message) const { itch::handle_message(message, book); }
};

using ShardPipeline = MessagePipeline<std::unique_ptr<itch::Message>, kQueueCapacity, ApplyToBook>;

}  // namespace

std::expected<std::unique_ptr<Book>, std::string> ShardedDecoder::decode_file(const std::filesystem::path& input_path,
                                                                                std::size_t shards)
{
    const auto file = MappedFile::open(input_path);
    if (!file) {
        return std::unexpected("decode_file: " + file.error());
    }
    if (shards == 0) {
        shards = 1;
    }

    std::vector<std::unique_ptr<Book>> books;
    std::vector<std::unique_ptr<ShardPipeline>> pipelines;
    for (std::size_t shard = 0; shard < shards; ++shard) {
        books.push_back(std::make_unique<Book>());
        pipelines.push_back(std::make_unique<ShardPipeline>(ApplyToBook{books.back().get()}));
    }

    std::thread parse_thread([&] {
        itch::FrameReader frames(file->bytes());
        while (const auto frame = frames.next()) {
            if (auto message = itch::decode_message(*frame)) {
                const std::size_t shard = message->stock_locate() % shards;
                pipelines[shard]->push(std::move(message));
            }
        }
    });
    parse_thread.join();
    for (auto& pipeline : pipelines) {
        pipeline->finish();
    }

    auto book = std::move(books.front());
    for (std::size_t shard = 1; shard < shards; ++shard) {
        book->absorb(std::move(*books[shard]));
    }
    return book;
}
