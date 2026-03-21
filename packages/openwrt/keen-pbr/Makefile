include $(TOPDIR)/rules.mk

PKG_NAME:=keen-pbr
PKG_VERSION:=3.0.0
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

define Package/keen-pbr
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Keen Policy-Based Routing
  DEPENDS:=+libcurl +libnl-core +libnl-route +libstdcpp +zlib +libzstd
endef

define Package/keen-pbr/description
  Policy-based routing daemon for Keen/OpenWRT routers.
endef

KEEN_PBR_SRC ?= /home/me/keen-pbr-src

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	cp -a $(KEEN_PBR_SRC)/. $(PKG_BUILD_DIR)/
endef

CMAKE_OPTIONS += \
	-DWITH_API=ON \
	-DCMAKE_BUILD_TYPE=MinSizeRel

define Package/keen-pbr/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/keen-pbr $(1)/usr/sbin/keen-pbr
endef

$(eval $(call BuildPackage,keen-pbr))
