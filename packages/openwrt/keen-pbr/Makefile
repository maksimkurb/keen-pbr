include $(TOPDIR)/rules.mk

KEEN_PBR_PACKAGE_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
KEEN_PBR_SRC ?= $(abspath $(KEEN_PBR_PACKAGE_DIR)/../../..)
KEEN_PBR_VERSION_FILE := $(firstword $(wildcard \
	$(KEEN_PBR_PACKAGE_DIR)/version.mk \
	$(KEEN_PBR_SRC)/version.mk \
	$(KEEN_PBR_PACKAGE_DIR)/../../../version.mk))
include $(KEEN_PBR_VERSION_FILE)
PKG_NAME:=keen-pbr
PKG_VERSION:=$(KEEN_PBR_VERSION)
PKG_RELEASE:=$(KEEN_PBR_RELEASE)

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

PKG_BUILD_DIR=$(BUILD_DIR)/$(PKG_NAME)$(if $(BUILD_VARIANT),-$(BUILD_VARIANT))/$(PKG_NAME)-$(PKG_VERSION)
CMAKE_BINARY_SUBDIR:=cmake-build

KEEN_PBR_FRONTEND_DIST ?= $(KEEN_PBR_SRC)/frontend/dist
KEEN_PBR_FULL_CONFIG ?= $(PKG_BUILD_DIR)/packages/common/config.full.example.json
KEEN_PBR_HEADLESS_CONFIG ?= $(PKG_BUILD_DIR)/packages/common/config.headless.example.json
KEEN_PBR_CMAKE_INSTALL_BINDIR ?= sbin
KEEN_PBR_STAGED_BINDIR ?= usr/$(KEEN_PBR_CMAKE_INSTALL_BINDIR)
KEEN_PBR_OBJCOPY ?= $(if $(TARGET_CROSS),$(TARGET_CROSS)objcopy,objcopy)
KEEN_PBR_DEBUG_ARTIFACT_DIR ?= $(BUILD_DIR)/debug-artifacts/$(PKG_NAME)$(if $(BUILD_VARIANT),-$(BUILD_VARIANT))/$(BUILD_VARIANT)
KEEN_PBR_STAGED_BIN ?= $(PKG_INSTALL_DIR)/$(KEEN_PBR_STAGED_BINDIR)/keen-pbr

define KeenPbr/EmitDebugSymbol
	@if [ -f "$(KEEN_PBR_STAGED_BIN)" ] && command -v "$(KEEN_PBR_OBJCOPY)" >/dev/null 2>&1; then \
		mkdir -p "$(KEEN_PBR_DEBUG_ARTIFACT_DIR)"; \
		"$(KEEN_PBR_OBJCOPY)" --only-keep-debug \
			"$(KEEN_PBR_STAGED_BIN)" \
			"$(KEEN_PBR_DEBUG_ARTIFACT_DIR)/keen-pbr.debug"; \
	fi
endef

#
# Common blocks
#

define Build/Prepare
	rm -rf $(PKG_BUILD_DIR)
	mkdir -p $(PKG_BUILD_DIR)
	rsync -a --delete \
		--exclude='frontend/node_modules' \
		--exclude='frontend/dist' \
		$(KEEN_PBR_SRC)/ $(PKG_BUILD_DIR)/
ifneq ($(BUILD_VARIANT),headless)
	mkdir -p $(PKG_BUILD_DIR)/frontend
	cp -a $(KEEN_PBR_FRONTEND_DIST) $(PKG_BUILD_DIR)/frontend/dist
	@test -d $(PKG_BUILD_DIR)/frontend/dist || (echo "Missing frontend/dist (provide frontend-build artifacts in KEEN_PBR_FRONTEND_DIST)"; false)
endif
endef

CMAKE_OPTIONS += \
	-DKEEN_PBR_DEFAULT_CONFIG_PATH:STRING=/etc/keen-pbr/config.json \
	-DKEEN_PBR_TARGET_OS:STRING=openwrt \
	-DCMAKE_INSTALL_BINDIR:PATH=$(KEEN_PBR_CMAKE_INSTALL_BINDIR) \
	-DCMAKE_BUILD_TYPE=MinSizeRel

ifneq ($(BUILD_VARIANT),headless)
  CMAKE_OPTIONS += \
	-DKEEN_PBR_FRONTEND_ROOT:STRING=/usr/share/keen-pbr/frontend \
	-DWITH_API=ON
else
  CMAKE_OPTIONS += \
	-DWITH_API=OFF
endif

#
# keen-pbr blocks
#

define Package/keen-pbr
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Keen Policy-Based Routing (full)
  VERSION:=$(if $(CONFIG_USE_APK),$(PKG_VERSION)-r$(PKG_RELEASE),$(PKG_VERSION)-$(PKG_RELEASE))
  DEPENDS:=+libatomic +libcurl +libnl-core +libnl-route +libstdcpp +zlib +libzstd
  VARIANT:=full
endef

define Package/keen-pbr/description
  Policy-based routing daemon for Keen/OpenWRT routers with REST API and WebUI.
endef

define Package/keen-pbr/conffiles
/etc/keen-pbr/config.json
/etc/keen-pbr/dnsmasq-fallback.conf
/etc/keen-pbr/local.lst
endef

define Package/keen-pbr/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_DIR) $(1)/etc/hotplug.d/firewall
	$(INSTALL_DIR) $(1)/etc/hotplug.d/iface
	$(INSTALL_DIR) $(1)/etc/keen-pbr
	$(INSTALL_DIR) $(1)/usr/lib/keen-pbr
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_DIR) $(1)/usr/share/keen-pbr/frontend
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/$(KEEN_PBR_STAGED_BINDIR)/keen-pbr $(1)/usr/sbin/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/init.d/keen-pbr $(1)/etc/init.d/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/hotplug.d/firewall/70-keen-pbr $(1)/etc/hotplug.d/firewall/70-keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/hotplug.d/iface/70-keen-pbr $(1)/etc/hotplug.d/iface/70-keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/usr/lib/keen-pbr/dnsmasq.sh $(1)/usr/lib/keen-pbr/dnsmasq.sh
	$(CP) $(PKG_BUILD_DIR)/frontend/dist/. $(1)/usr/share/keen-pbr/frontend/
	chmod -R a=rX,u+w $(1)/usr/share/keen-pbr/frontend
	$(INSTALL_CONF) $(KEEN_PBR_FULL_CONFIG) $(1)/etc/keen-pbr/config.json
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/keen-pbr/dnsmasq-fallback.conf $(1)/etc/keen-pbr/dnsmasq-fallback.conf
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/common/local.lst $(1)/etc/keen-pbr/local.lst
	$(call KeenPbr/EmitDebugSymbol)
endef

define Package/keen-pbr/postinst
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/postinst)
endef

define Package/keen-pbr/prerm
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/prerm)
endef

#
# keen-pbr-headless blocks
#

define Package/keen-pbr-headless
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Keen Policy-Based Routing (headless)
  VERSION:=$(if $(CONFIG_USE_APK),$(PKG_VERSION)-r$(PKG_RELEASE),$(PKG_VERSION)-$(PKG_RELEASE))
  DEPENDS:=+libatomic +libcurl +libnl-core +libnl-route +libstdcpp +zlib +libzstd
  VARIANT:=headless
endef

define Package/keen-pbr-headless/description
  Policy-based routing daemon for Keen/OpenWRT routers without REST API and WebUI.
endef

define Package/keen-pbr-headless/conffiles
/etc/keen-pbr/config.json
/etc/keen-pbr/dnsmasq-fallback.conf
/etc/keen-pbr/local.lst
endef

define Package/keen-pbr-headless/install
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_DIR) $(1)/etc/hotplug.d/firewall
	$(INSTALL_DIR) $(1)/etc/hotplug.d/iface
	$(INSTALL_DIR) $(1)/etc/keen-pbr
	$(INSTALL_DIR) $(1)/usr/lib/keen-pbr
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/$(KEEN_PBR_STAGED_BINDIR)/keen-pbr $(1)/usr/sbin/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/init.d/keen-pbr $(1)/etc/init.d/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/hotplug.d/firewall/70-keen-pbr $(1)/etc/hotplug.d/firewall/70-keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/hotplug.d/iface/70-keen-pbr $(1)/etc/hotplug.d/iface/70-keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/usr/lib/keen-pbr/dnsmasq.sh $(1)/usr/lib/keen-pbr/dnsmasq.sh
	$(INSTALL_CONF) $(KEEN_PBR_HEADLESS_CONFIG) $(1)/etc/keen-pbr/config.json
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/keen-pbr/dnsmasq-fallback.conf $(1)/etc/keen-pbr/dnsmasq-fallback.conf
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/common/local.lst $(1)/etc/keen-pbr/local.lst
	$(call KeenPbr/EmitDebugSymbol)
endef

define Package/keen-pbr-headless/postinst
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/postinst)
endef

define Package/keen-pbr-headless/prerm
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/prerm)
endef

$(eval $(call BuildPackage,keen-pbr))
$(eval $(call BuildPackage,keen-pbr-headless))
