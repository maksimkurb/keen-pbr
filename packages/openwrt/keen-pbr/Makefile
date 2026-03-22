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

define Build/Compile
	(cd $(PKG_BUILD_DIR)/frontend && bun install --frozen-lockfile && bun run build)
	$(call Build/Compile/Default)
endef

CMAKE_OPTIONS += \
	-DKEEN_PBR_DEFAULT_CONFIG_PATH:STRING=/etc/keen-pbr/config.json \
	-DKEEN_PBR_FRONTEND_ROOT:STRING=/usr/share/keen-pbr/frontend \
	-DWITH_API=ON \
	-DCMAKE_BUILD_TYPE=MinSizeRel

define Package/keen-pbr/conffiles
/etc/keen-pbr/config.json
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
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/usr/bin/keen-pbr $(1)/usr/sbin/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/init.d/keen-pbr $(1)/etc/init.d/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/hotplug.d/firewall/70-keen-pbr $(1)/etc/hotplug.d/firewall/70-keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/etc/hotplug.d/iface/70-keen-pbr $(1)/etc/hotplug.d/iface/70-keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/usr/lib/keen-pbr/dnsmasq.sh $(1)/usr/lib/keen-pbr/dnsmasq.sh
	(cd $(PKG_BUILD_DIR)/frontend/dist && find . -type f -name '*.gz' -exec cp --parents {} $(1)/usr/share/keen-pbr/frontend/ \;)
	chmod -R a=rX,u+w $(1)/usr/share/keen-pbr/frontend
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/config.example.json $(1)/etc/keen-pbr/config.json
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/common/local.lst $(1)/etc/keen-pbr/local.lst
endef

define Package/keen-pbr/postinst
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/postinst)
endef

define Package/keen-pbr/prerm
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/prerm)
endef

define Package/keen-pbr/postrm
$(file <$(PKG_BUILD_DIR)/packages/openwrt/keen-pbr/files/postrm)
endef

$(eval $(call BuildPackage,keen-pbr))
