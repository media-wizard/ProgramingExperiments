g++ -o way way.cpp xdg-shell-protocol.o $(pkg-config --cflags --libs wayland-client wayland-egl egl glesv2)

