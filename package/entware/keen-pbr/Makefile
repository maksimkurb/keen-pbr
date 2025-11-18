include $(TOPDIR)/rules.mk

PKG_NAME:=keen-pbr
PKG_VERSION:=$(shell cat ../../../VERSION)
PKG_REV:=$(shell echo ${GITHUB_SHA})
PKG_RELEASE:=1

PKG_SOURCE_URL:=https://github.com/maksimkurb/keen-pbr.git
PKG_SOURCE_PROTO:=git
PKG_SOURCE_VERSION:=$(PKG_REV)

PKG_BUILD_PARALLEL:=1

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/golang.mk

define Package/keen-pbr
	SECTION:=net
	CATEGORY:=Networking
	EXTRA_DEPENDS:=dnsmasq-full, ipset, iptables, cron
	TITLE:=Tool for downloading, parsing and importing domains/ip/cidr lists into dnsmasq and ipset
	URL:=https://github.com/maksimkurb/keen-pbr/
	MAINTAINER:=Maxim Kurbatov <maxim@kurb.me>
	VERSION:=$(PKG_VERSION)-$(BOARD)-$(PKG_RELEASE)
endef

define Package/keen-pbr/description
	Policy-based routing toolkit for Keenetic routers
endef

GO_TARGET:=./src/cmd/keen-pbr

define Build/Compile
	# Build frontend
	cd $(PKG_BUILD_DIR)/src/frontend && \
		bun install && \
		bun run build

	# Build Go binary
	$(call Build/Compile/Default)
endef

define Package/keen-pbr/conffiles
/opt/etc/keen-pbr/keen-pbr.conf
/opt/etc/keen-pbr/local.lst
endef

define Package/keen-pbr/install
		$(INSTALL_DIR) $(1)/opt/etc/cron.daily/
		$(INSTALL_DIR) $(1)/opt/etc/ndm/netfilter.d/
		$(INSTALL_DIR) $(1)/opt/etc/ndm/ifstatechanged.d/
		$(INSTALL_DIR) $(1)/opt/etc/init.d/
		$(INSTALL_DIR) $(1)/opt/etc/keen-pbr/
		$(INSTALL_DIR) $(1)/opt/etc/dnsmasq.d/
		$(INSTALL_DIR) $(1)/opt/usr/bin/

		echo "> INSTALL DIR = $(PKG_BUILD_DIR)"
		ls -la $(PKG_BUILD_DIR)

		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/cron.daily/50-keen-pbr-lists-update.sh $(1)/opt/etc/cron.daily/
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/ndm/ifstatechanged.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/ifstatechanged.d/
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/ndm/netfilter.d/50-keen-pbr-fwmarks.sh $(1)/opt/etc/ndm/netfilter.d/
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/init.d/S80keen-pbr $(1)/opt/etc/init.d
		$(INSTALL_CONF) $(PKG_BUILD_DIR)/package/etc/keen-pbr/keen-pbr.conf $(1)/opt/etc/keen-pbr/
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/dnsmasq.d/100-keen-pbr.conf $(1)/opt/etc/dnsmasq.d/
		$(INSTALL_CONF) $(PKG_BUILD_DIR)/package/etc/keen-pbr/local.lst $(1)/opt/etc/keen-pbr/
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/keen-pbr/defaults $(1)/opt/etc/keen-pbr/
		$(INSTALL_CONF) $(PKG_BUILD_DIR)/package/etc/dnsmasq.conf.keen-pbr $(1)/opt/etc/
		$(INSTALL_BIN) $(PKG_INSTALL_DIR)/bin/keen-pbr $(1)/opt/usr/bin/
endef

define Package/keen-pbr/postinst
$(file <$(PKG_BUILD_DIR)/package/entware/keen-pbr/postinst)
endef

define Package/keen-pbr/postrm
$(file <$(PKG_BUILD_DIR)/package/entware/keen-pbr/postrm)
endef

$(eval $(call BuildPackage,keen-pbr))