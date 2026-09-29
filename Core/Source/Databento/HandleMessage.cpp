#include "Databento/HandleMessage.h"

#include "Core/Logger.h"
#include "Databento/MboMessage.h"

namespace databento {

void handle_message(const std::unique_ptr<itch::Message>& message, Book* book)
{
    if (message == nullptr || book == nullptr) {
        return;
    }

    const auto* mbo = dynamic_cast<const MboMessage*>(message.get());
    if (mbo == nullptr) {
        return;
    }

    switch (mbo->action()) {
    case Action::Add:
        Logger::debug("databento::handle_message: Add not yet wired to SecurityBook");
        break;
    case Action::Fill:
        Logger::debug("databento::handle_message: Fill not yet wired to SecurityBook");
        break;
    case Action::Cancel:
        Logger::debug("databento::handle_message: Cancel not yet wired to SecurityBook");
        break;
    case Action::Modify:
        Logger::debug("databento::handle_message: Modify not yet wired to SecurityBook");
        break;
    case Action::Clear:
        Logger::debug("databento::handle_message: Clear not yet wired to SecurityBook");
        break;
    case Action::Trade:
        break;
    case Action::None:
        break;
    }
}

}  // namespace databento
