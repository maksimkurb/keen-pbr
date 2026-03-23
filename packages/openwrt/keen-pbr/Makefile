include $(TOPDIR)/rules.mk

PKG_NAME:=keen-pbr
PKG_VERSION:=3.0.0
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

PKG_BUILD_DIR=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)$(if $(BUILD_VARIANT),-$(BUILD_VARIANT))

KEEN_PBR_SRC ?= /home/me/keen-pbr-src
KEEN_PBR_FRONTEND_DIST ?= $(KEEN_PBR_SRC)/frontend/dist
KEEN_PBR_HEADLESS_CONFIG ?= $(PKG_BUILD_DIR)/packages/common/config.headless.example.json

define Package/keen-pbr/Default
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Keen Policy-Based Routing
  DEPENDS:=+libcurl +libnl-core +libnl-route +libstdcpp +zlib +libzstd
endef

define Package/keen-pbr
  $(call Package/keen-pbr/Default)
  TITLE+= (full)
endef

define Package/keen-pbr/description
  Policy-based routing daemon for Keen/OpenWRT routers with REST API and WebUI.
endef

define Package/keen-pbr-headless
  $(call Package/keen-pbr/Default)
  TITLE+= (headless)
  VARIANT:=headless
endef

define Package/keen-pbr-headless/description
  Policy-based routing daemon for Keen/OpenWRT routers without REST API and WebUI.
endef

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
	-DCMAKE_BUILD_TYPE=MinSizeRel

ifneq ($(BUILD_VARIANT),headless)
  CMAKE_OPTIONS += \
	-DKEEN_PBR_FRONTEND_ROOT:STRING=/usr/share/keen-pbr/frontend \
	-DWITH_API=ON
else
  CMAKE_OPTIONS += \
	-DWITH_API=OFF
endif

define Package/keen-pbr/conffiles
/etc/keen-pbr/config.json
/etc/keen-pbr/local.lst
endef

define Package/keen-pbr-headless/conffiles
/etc/keen-pbr/config.json
/etc/keen-pbr/local.lst
endef

define Package/keen-pbr/install/common
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_DIR) $(1)/etc/hotplug.d/firewall
	$(INSTALL_DIR) $(1)/etc/hotplug.d/iface
	$(INSTALL_DIR) $(1)/etc/keen-pbr
	$(INSTALL_DIR) $(1)/usr/lib/keen-pbr
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/keen-pbr $(1)/usr/sbin/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/init.d/keen-pbr $(1)/etc/init.d/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/hotplug.d/firewall/70-keen-pbr $(1)/etc/hotplug.d/firewall/70-keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/hotplug.d/iface/70-keen-pbr $(1)/etc/hotplug.d/iface/70-keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/usr/lib/keen-pbr/dnsmasq.sh $(1)/usr/lib/keen-pbr/dnsmasq.sh
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/common/local.lst $(1)/etc/keen-pbr/local.lst
endef

define Package/keen-pbr/install
	$(call Package/keen-pbr/install/common,$(1))
	$(INSTALL_DIR) $(1)/usr/share/keen-pbr/frontend
	$(CP) $(PKG_BUILD_DIR)/frontend/dist/. $(1)/usr/share/keen-pbr/frontend/
	chmod -R a=rX,u+w $(1)/usr/share/keen-pbr/frontend
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/config.example.json $(1)/etc/keen-pbr/config.json
endef

define Package/keen-pbr-headless/install
	$(call Package/keen-pbr/install/common,$(1))
	$(INSTALL_CONF) $(KEEN_PBR_HEADLESS_CONFIG) $(1)/etc/keen-pbr/config.json
endef

define Package/keen-pbr/postinst
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/postinst)
endef

define Package/keen-pbr-headless/postinst
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/postinst)
endef

define Package/keen-pbr/prerm
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/prerm)
endef

define Package/keen-pbr-headless/prerm
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/prerm)
endef

define Package/keen-pbr/postrm
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/postrm)
endef

define Package/keen-pbr-headless/postrm
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/postrm)
endef

$(eval $(call BuildPackage,keen-pbr))
$(eval $(call BuildPackage,keen-pbr-headless))
