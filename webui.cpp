#include "app.hpp"
#include "webui/include/webui.hpp"
int main(int argc, char *argv[])
{
    try
    {
        std::thread server_thread([&]()
                                  {
        app::init_app("88888888");
        boost::asio::io_context io_context;
        rt::router router;
        app::server::register_routes(router);
        servic::Server server(io_context, 8080);
        server.run(router); });

        webui::window my_window;
        my_window.show("http://localhost:8080/");
        webui::wait();

        server_thread.join();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}