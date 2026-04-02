# Makefile for NGINX JWT Module
# This builds the module as a dynamic module

NGINX_VERSION ?= 1.24.0
NGINX_SRC ?= nginx-$(NGINX_VERSION)
MODULE_DIR = $(shell pwd)

.PHONY: all download configure build install clean

all: build

download:
	@echo "Downloading NGINX $(NGINX_VERSION)..."
	@if [ ! -d "$(NGINX_SRC)" ]; then \
		wget http://nginx.org/download/nginx-$(NGINX_VERSION).tar.gz && \
		tar -xzf nginx-$(NGINX_VERSION).tar.gz && \
		rm nginx-$(NGINX_VERSION).tar.gz; \
	fi

configure: download
	@echo "Configuring NGINX with JWT module..."
	cd $(NGINX_SRC) && ./configure \
		--with-compat \
		--add-dynamic-module=$(MODULE_DIR)

build: configure
	@echo "Building NGINX JWT module..."
	cd $(NGINX_SRC) && make modules

install:
	@echo "Installing module..."
	@if [ -z "$(NGINX_MODULES_PATH)" ]; then \
		echo "Please set NGINX_MODULES_PATH to your NGINX modules directory"; \
		echo "Example: make install NGINX_MODULES_PATH=/usr/lib/nginx/modules"; \
		exit 1; \
	fi
	@cp $(NGINX_SRC)/objs/ngx_http_jwt_module.so $(NGINX_MODULES_PATH)/
	@echo "Module installed to $(NGINX_MODULES_PATH)/ngx_http_jwt_module.so"

clean:
	@echo "Cleaning build files..."
	@rm -rf $(NGINX_SRC)

deps:
	@echo "Installing dependencies (Ubuntu/Debian)..."
	sudo apt-get update
	sudo apt-get install -y build-essential libpcre3-dev zlib1g-dev libssl-dev libjwt-dev libjansson-dev wget

deps-fedora:
	@echo "Installing dependencies (Fedora/RHEL)..."
	sudo dnf install -y gcc make pcre-devel zlib-devel openssl-devel libjwt-devel jansson-devel wget

deps-arch:
	@echo "Installing dependencies (Arch Linux)..."
	sudo pacman -S --needed base-devel pcre zlib openssl libjwt jansson wget

help:
	@echo "NGINX JWT Module Build System"
	@echo ""
	@echo "Usage:"
	@echo "  make deps           - Install dependencies (Ubuntu/Debian)"
	@echo "  make deps-fedora    - Install dependencies (Fedora/RHEL)"
	@echo "  make deps-arch      - Install dependencies (Arch Linux)"
	@echo "  make download       - Download NGINX source"
	@echo "  make configure      - Configure NGINX with JWT module"
	@echo "  make build          - Build the JWT module"
	@echo "  make install        - Install module (set NGINX_MODULES_PATH)"
	@echo "  make clean          - Clean build files"
	@echo ""
	@echo "Environment Variables:"
	@echo "  NGINX_VERSION       - NGINX version to use (default: 1.24.0)"
	@echo "  NGINX_MODULES_PATH  - Path to install module (required for install)"
	@echo ""
	@echo "Example:"
	@echo "  make deps-arch"
	@echo "  make build"
	@echo "  sudo make install NGINX_MODULES_PATH=/usr/lib/nginx/modules"
