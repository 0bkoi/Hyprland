#include <protocols/ToplevelExport.hpp>
#include <Compositor.hpp>
#include <cstdint>
#include <gtest/gtest.h>
#include <hyprutils/os/FileDescriptor.hpp>
#include <initializer_list>
#include <memory>
#include <sys/socket.h>
#include <wayland-server-core.h>
#include <array>

class CToplevelExportTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_display.reset(wl_display_create());
        ASSERT_NE(m_display, nullptr);

        m_previousCompositor       = std::move(g_pCompositor);
        g_pCompositor              = makeUnique<CCompositor>(true);
        g_pCompositor->m_wlDisplay = m_display.get();

        std::array<int, 2> sockets = {-1, -1};
        ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets.data()), 0);

        Hyprutils::OS::CFileDescriptor serverSocket{sockets[0]};
        m_socket = Hyprutils::OS::CFileDescriptor{sockets[1]};
        m_client = wl_client_create(m_display.get(), serverSocket.get());
        ASSERT_NE(m_client, nullptr);
        serverSocket.take();

        m_previousToplevelExport = std::move(PROTO::toplevelExport);
        PROTO::toplevelExport    = makeUnique<CToplevelExportProtocol>(&hyprland_toplevel_export_manager_v1_interface, 2, "hyprland_toplevel_export_manager_v1_interface");
        PROTO::toplevelExport->bindManager(m_client, nullptr, 2, 2);
    }

    void TearDown() override {
        if (m_display)
            wl_display_destroy_clients(m_display.get());
        PROTO::toplevelExport = std::move(m_previousToplevelExport);
        g_pCompositor         = std::move(m_previousCompositor);
    }

    void sendToplevelExportRequest(std::initializer_list<uint32_t> req) {
        const auto size = req.size() * sizeof(uint32_t);
        ASSERT_EQ(send(m_socket.get(), req.begin(), size, MSG_NOSIGNAL), sc<ssize_t>(size));
        ASSERT_EQ(wl_event_loop_dispatch(wl_display_get_event_loop(m_display.get()), 0), 0);
    }

    std::unique_ptr<wl_display, decltype(&wl_display_destroy)> m_display{nullptr, wl_display_destroy};
    wl_client*                                                 m_client = nullptr;
    Hyprutils::OS::CFileDescriptor                             m_socket;
    UP<CCompositor>                                            m_previousCompositor;
    UP<CToplevelExportProtocol>                                m_previousToplevelExport;
};

TEST_F(CToplevelExportTest, SendFailedWhenCaptureMissingWindow) {
    sendToplevelExportRequest({2, (20 << 16) | 0, 3, 0, 0xdead}); // a window address doesn't exist
    wl_client_flush(m_client);

    ASSERT_NE(wl_client_get_object(m_client, 3), nullptr);
    const std::array<uint32_t, 2> eventFailedExpected{3, (8 << 16) | 4};
    std::array<uint32_t, 2>       eventReceived = {};
    ASSERT_EQ(recv(m_socket.get(), eventReceived.data(), sizeof(eventReceived), MSG_DONTWAIT), 8);
    ASSERT_EQ(eventReceived, eventFailedExpected);

    sendToplevelExportRequest({3, (8 << 16) | 1});
    ASSERT_EQ(wl_client_get_object(m_client, 3), nullptr);
}
