// An fcitx5 addon that tells floatkey when a text field gains or loses focus,
// so the on-screen keyboard can come up by itself, as on an iPad. fcitx5 hears
// about focus from every kind of app (GTK, Qt, Electron, terminals, X11), so
// floatkey doesn't have to be the input method itself.

#include <cstdlib>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include <fcitx-utils/event.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/instance.h>

#include "fieldtype.h"

namespace {

// The socket floatkey listens on beside the Wayland display (see src/ipc.cpp).
std::string socketPath()
{
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    const char *display = std::getenv("WAYLAND_DISPLAY");
    std::string name = display && *display ? display : "wayland-0";
    for (char &c : name)
        if (c == '/')
            c = '_';
    return std::string(runtime && *runtime ? runtime : "/tmp") + "/floatkey-" + name + ".sock";
}

// One line to floatkey, without ever making fcitx5 wait: if floatkey isn't
// running, or is too busy to take the connection, the line is dropped.
void tell(const std::string &line)
{
    const std::string path = socketPath();
    sockaddr_un address{};
    if (path.size() >= sizeof address.sun_path)
        return;
    address.sun_family = AF_UNIX;
    path.copy(address.sun_path, path.size());
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return;
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0)
        send(fd, line.data(), line.size(), MSG_NOSIGNAL);
    close(fd);
}

class FloatkeyFocus : public fcitx::AddonInstance {
public:
    explicit FloatkeyFocus(fcitx::Instance *instance) : instance_(instance)
    {
        for (const auto type : {fcitx::EventType::InputContextFocusIn, fcitx::EventType::InputContextFocusOut,
                                fcitx::EventType::InputContextCapabilityChanged,
                                fcitx::EventType::InputContextDestroyed}) {
            handlers_.emplace_back(instance->watchEvent(type, fcitx::EventWatcherPhase::Default,
                                                        [this](fcitx::Event &event) {
                                                            if (event.type() == fcitx::EventType::InputContextFocusIn)
                                                                focusedAgain_ = true;
                                                            schedule();
                                                        }));
        }
    }

private:
    // Focus moves in bursts (out of one field, into another, hints updated)
    // and apps on different frontends report it out of order, so look at
    // where focus ended up once the burst is over. A field that took focus
    // is reported even when it's the same kind as the last one: a keyboard
    // swiped away in one field comes back in the next.
    void schedule()
    {
        if (pending_)
            return;
        pending_ = true;
        check_ = instance_->eventLoop().addDeferEvent([this](fcitx::EventSource *) {
            pending_ = false;
            report();
            return true;
        });
    }

    void report()
    {
        fcitx::InputContext *ic = instance_->lastFocusedInputContext();
        std::string state = "blur";
        if (ic && ic->hasFocus() && floatkey::wantsKeyboard(ic->capabilityFlags()))
            state = std::string("focus ") + floatkey::fieldType(ic->capabilityFlags());
        const bool focusedAgain = focusedAgain_;
        focusedAgain_ = false;
        if (state == last_ && !(focusedAgain && state != "blur"))
            return;
        last_ = state;
        tell(state + "\n");
    }

    fcitx::Instance *instance_;
    std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>> handlers_;
    std::unique_ptr<fcitx::EventSource> check_;
    bool pending_ = false;
    bool focusedAgain_ = false;
    std::string last_ = "blur";
};

class FloatkeyFocusFactory : public fcitx::AddonFactory {
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override
    {
        return new FloatkeyFocus(manager->instance());
    }
};

}

FCITX_ADDON_FACTORY_V2_BACKWARDS(floatkey, FloatkeyFocusFactory)
