# Thin wrapper around idf.py. Run inside the dev shell (direnv or `nix develop`).
# Goals run in order, so `make flash monitor` flashes and then opens the monitor.
# Pick a serial port with `make flash PORT=/dev/ttyACM0`.

IDF_PY ?= idf.py
PORT ?=
MERGED_BIN ?= merged.bin

idf := $(IDF_PY)$(if $(PORT), -p $(PORT))

.DEFAULT_GOAL := build
.NOTPARALLEL:
.PHONY: help build flash monitor menuconfig size merge-bin erase clean fullclean fmt

help: ## List the targets
	@grep -E '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | awk -F ':.*## ' '{ printf "  %-12s %s\n", $$1, $$2 }'

build: ## Build the firmware
	$(idf) build

flash: ## Build and flash the firmware
	$(idf) flash

monitor: ## Open the serial monitor (leave with Ctrl+])
	$(idf) monitor

menuconfig: ## Edit the local sdkconfig
	$(idf) menuconfig

size: ## Show the firmware size summary
	$(idf) size

merge-bin: ## Build the full flash image (offset 0x0) like the release
	$(idf) build merge-bin --format raw --output $(MERGED_BIN)

erase: ## Erase the whole flash, including NVS settings and pairing
	$(idf) erase-flash

clean: ## Remove build outputs
	$(idf) clean

fullclean: ## Remove the whole build directory
	$(idf) fullclean

fmt: ## Format the Nix files
	nix fmt
