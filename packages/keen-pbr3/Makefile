include $(TOPDIR)/rules.mk

PKG_NAME:=keen-pbr3
PKG_VERSION:=3.0.0
PKG_RELEASE:=1

PKG_BUILD_DEPENDS:=meson/host ninja/host

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/meson.mk

define Package/keen-pbr3
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Keen Policy-Based Routing
  DEPENDS:=+libcurl +libnl-core +libnl-route +libmbedtls
endef

define Package/keen-pbr3/description
  Policy-based routing daemon for Keen/OpenWRT routers.
endef

NLOHMANN_JSON_VERSION:=3.12.0
CPP_HTTPLIB_VERSION:=0.31.0

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp -a /home/me/keen-pbr3-src/. $(PKG_BUILD_DIR)/
	# Remove subprojects so meson doesn't try to use wraps
	rm -rf $(PKG_BUILD_DIR)/subprojects

	# Install nlohmann/json headers into staging
	mkdir -p $(STAGING_DIR)/usr/include/nlohmann
	wget -q -O $(DL_DIR)/nlohmann_json-$(NLOHMANN_JSON_VERSION).zip \
		"https://github.com/nlohmann/json/releases/download/v$(NLOHMANN_JSON_VERSION)/include.zip"
	unzip -o $(DL_DIR)/nlohmann_json-$(NLOHMANN_JSON_VERSION).zip \
		"include/nlohmann/*" -d $(BUILD_DIR)/nlohmann_json-tmp
	cp -a $(BUILD_DIR)/nlohmann_json-tmp/include/nlohmann/. \
		$(STAGING_DIR)/usr/include/nlohmann/
	rm -rf $(BUILD_DIR)/nlohmann_json-tmp

	# Create nlohmann_json pkg-config file
	mkdir -p $(STAGING_DIR)/usr/lib/pkgconfig
	printf '%s\n' \
		'prefix=/usr' \
		'includedir=$${prefix}/include' \
		'' \
		'Name: nlohmann_json' \
		'Description: JSON for Modern C++' \
		'Version: $(NLOHMANN_JSON_VERSION)' \
		'Cflags: -I$${includedir}' \
		> $(STAGING_DIR)/usr/lib/pkgconfig/nlohmann_json.pc

	# Install cpp-httplib header into staging
	wget -q -O $(DL_DIR)/cpp-httplib-$(CPP_HTTPLIB_VERSION).tar.gz \
		"https://github.com/yhirose/cpp-httplib/archive/refs/tags/v$(CPP_HTTPLIB_VERSION).tar.gz"
	tar -xzf $(DL_DIR)/cpp-httplib-$(CPP_HTTPLIB_VERSION).tar.gz \
		-C $(BUILD_DIR) cpp-httplib-$(CPP_HTTPLIB_VERSION)/httplib.h
	cp $(BUILD_DIR)/cpp-httplib-$(CPP_HTTPLIB_VERSION)/httplib.h \
		$(STAGING_DIR)/usr/include/httplib.h
	rm -rf $(BUILD_DIR)/cpp-httplib-$(CPP_HTTPLIB_VERSION)

	# Create cpp-httplib pkg-config file
	printf '%s\n' \
		'prefix=/usr' \
		'includedir=$${prefix}/include' \
		'' \
		'Name: cpp-httplib' \
		'Description: C++ header-only HTTP/HTTPS server and client library' \
		'Version: $(CPP_HTTPLIB_VERSION)' \
		'Cflags: -I$${includedir}' \
		> $(STAGING_DIR)/usr/lib/pkgconfig/cpp-httplib.pc

	# Create mbedtls pkg-config file (OpenWrt's libmbedtls doesn't ship one)
	printf '%s\n' \
		'prefix=/usr' \
		'exec_prefix=$${prefix}' \
		'libdir=$${exec_prefix}/lib' \
		'includedir=$${prefix}/include' \
		'' \
		'Name: mbedtls' \
		'Description: Mbed TLS' \
		'Version: 3.6.0' \
		'Libs: -L$${libdir} -lmbedtls -lmbedx509 -lmbedcrypto' \
		'Cflags: -I$${includedir}' \
		> $(STAGING_DIR)/usr/lib/pkgconfig/mbedtls.pc
endef

MESON_ARGS += \
	-Dwith_api=true \
	-Dcpp_std=c++20 \
	-Doptimization=s \
	-Db_lto=true

define Package/keen-pbr3/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/keen-pbr3 $(1)/usr/sbin/keen-pbr3
endef

$(eval $(call BuildPackage,keen-pbr3))
