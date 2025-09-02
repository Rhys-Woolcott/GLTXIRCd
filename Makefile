APPNAME = GLTXIRC
CXX = g++
CXXFLAGS = -Wall -Wextra -O2

SRCDIR = src
BINDIR = bin

SERVER_SRC = $(SRCDIR)/gltxirc.cpp
CLIENT_SRC = $(SRCDIR)/client.cpp

SERVER_BIN = $(BINDIR)/gltxirc
CLIENT_BIN = $(BINDIR)/client

.PHONY: all clean

all: $(SERVER_BIN) $(CLIENT_BIN)

$(BINDIR):
	mkdir -p $(BINDIR)

$(SERVER_BIN): $(SERVER_SRC) | $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(CLIENT_BIN): $(CLIENT_SRC) | $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -rf $(BINDIR)
