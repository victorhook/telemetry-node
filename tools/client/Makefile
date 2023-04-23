CLIENT_SRC_DIR = src
CLIENT_BUILD_DIR = build

CLIENT_SRC = $(CLIENT_SRC_DIR)client.c $(CLIENT_SRC_DIR)go.c
CLIENT_OBJ = $(patsubst $(CLIENT_SRC_DIR)%.c,$(CLIENT_BUILD_DIR)/%.o,$(CLIENT_SRC))
CLIENT_DEPS = $(CLIENT_SRC_DIR)log.h
CLIENT_TARGET = telemetry_client
CLIENT_CC = gcc
CLIENT_INCLUDE = include
CLIENT_CFLAGS = -Wall -I $(CLIENT_INCLUDE)


client: $(CLIENT_TARGET)
	@chmod +x $^


$(CLIENT_TARGET): $(CLIENT_OBJ)
	$(CLIENT_CC) -o $@ $^ $(CLIENT_CFLAGS)


$(CLIENT_BUILD_DIR)/%.o: $(CLIENT_SRC_DIR)/%.c $(DEPS)
	@echo CC $<
	@$(CLIENT_CC) -c -o $@ $< $(CLIENT_CFLAGS)

clean:
	rm -rf $(CLIENT_OBJ)
	rm -rf $(CLIENT_TARGET)