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
	-DKEEN_PBR_DEFAULT_CONFIG_PATH:STRING=/opt/etc/keen-pbr/config.json \
	-DKEEN_PBR_FRONTEND_ROOT:STRING=/opt/usr/share/keen-pbr/frontend \
	-DWITH_API=ON \
	-DCMAKE_BUILD_TYPE=MinSizeRel

define Package/keen-pbr/conffiles
/opt/etc/keen-pbr/config.json
/opt/etc/keen-pbr/local.lst
endef

define Package/keen-pbr/install
	$(INSTALL_DIR) $(1)/opt/etc/init.d
	$(INSTALL_DIR) $(1)/opt/etc/dnsmasq.d
	$(INSTALL_DIR) $(1)/opt/etc/keen-pbr
	$(INSTALL_DIR) $(1)/opt/etc/ndm/iflayerchanged.d
	$(INSTALL_DIR) $(1)/opt/etc/ndm/ifstatechanged.d
	$(INSTALL_DIR) $(1)/opt/etc/ndm/netfilter.d
	$(INSTALL_DIR) $(1)/opt/usr/bin
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/opt/bin/keen-pbr $(1)/opt/usr/bin/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/init.d/S80keen-pbr $(1)/opt/etc/init.d/S80keen-pbr
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/dnsmasq.d/dnsmasq.keen-pbr.conf $(1)/opt/etc/dnsmasq.d/dnsmasq.keen-pbr.conf
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/defaults $(1)/opt/etc/keen-pbr/defaults
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/dnsmasq.sh $(1)/opt/etc/keen-pbr/dnsmasq.sh
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/dnsmasq.conf.template $(1)/opt/etc/keen-pbr/dnsmasq.conf.template
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/ndm/iflayerchanged.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/iflayerchanged.d/50-keen-pbr-routing.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/ndm/ifstatechanged.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/ifstatechanged.d/50-keen-pbr-routing.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/ndm/netfilter.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/netfilter.d/50-keen-pbr-routing.sh
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/config.example.json $(1)/opt/etc/keen-pbr/config.json
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/common/local.lst $(1)/opt/etc/keen-pbr/local.lst
endef

define Package/keen-pbr/postinst
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/postinst)
endef

define Package/keen-pbr/prerm
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/prerm)
endef

define Package/keen-pbr/postrm
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/postrm)
endef

$(eval $(call BuildPackage,keen-pbr))
