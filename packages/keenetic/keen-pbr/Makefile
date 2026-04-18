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

KEEN_PBR_FRONTEND_DIST ?= $(KEEN_PBR_SRC)/frontend/dist
KEEN_PBR_FULL_CONFIG ?= $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/config.full.example.json
KEEN_PBR_HEADLESS_CONFIG ?= $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/config.headless.example.json
KEEN_PBR_CMAKE_INSTALL_BINDIR ?= usr/bin
KEEN_PBR_STAGED_BINDIR ?= opt/$(KEEN_PBR_CMAKE_INSTALL_BINDIR)

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
	-DKEEN_PBR_DEFAULT_CONFIG_PATH:STRING=/opt/etc/keen-pbr/config.json \
	-DKEEN_PBR_SYSTEM_RESOLVER_HOOK:STRING=/opt/usr/lib/keen-pbr/dnsmasq.sh \
	-DKEEN_PBR_TARGET_OS:STRING=keenetic \
	-DCMAKE_INSTALL_BINDIR:PATH=$(KEEN_PBR_CMAKE_INSTALL_BINDIR) \
	-DUSE_KEENETIC_API=ON \
	-DCMAKE_BUILD_TYPE=MinSizeRel

ifneq ($(BUILD_VARIANT),headless)
  CMAKE_OPTIONS += \
	-DKEEN_PBR_FRONTEND_ROOT:STRING=/opt/usr/share/keen-pbr/frontend \
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
  VERSION:=$(PKG_VERSION)-$(PKG_RELEASE)
  DEPENDS:=+libatomic +libcurl +libnl-core +libnl-route +libstdcpp +zlib +libzstd
  VARIANT:=full
endef

define Package/keen-pbr/description
  Policy-based routing daemon for Keen/OpenWRT routers with REST API and WebUI.
endef

define Package/keen-pbr/conffiles
/opt/etc/keen-pbr/config.json
/opt/etc/keen-pbr/local.lst
/opt/etc/keen-pbr/dnsmasq-fallback.conf
endef

define Package/keen-pbr/install
	$(INSTALL_DIR) $(1)/opt/etc/init.d
	$(INSTALL_DIR) $(1)/opt/etc/dnsmasq.d
	$(INSTALL_DIR) $(1)/opt/etc/keen-pbr
	$(INSTALL_DIR) $(1)/opt/etc/ndm/iflayerchanged.d
	$(INSTALL_DIR) $(1)/opt/etc/ndm/ifstatechanged.d
	$(INSTALL_DIR) $(1)/opt/etc/ndm/netfilter.d
	$(INSTALL_DIR) $(1)/opt/usr/bin
	$(INSTALL_DIR) $(1)/opt/usr/lib/keen-pbr
	$(INSTALL_DIR) $(1)/opt/usr/share/keen-pbr/frontend
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/$(KEEN_PBR_STAGED_BINDIR)/keen-pbr $(1)/opt/usr/bin/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/init.d/S80keen-pbr $(1)/opt/etc/init.d/S80keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/defaults $(1)/opt/etc/keen-pbr/defaults
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/dnsmasq-fallback.conf $(1)/opt/etc/keen-pbr/dnsmasq-fallback.conf
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/usr/lib/keen-pbr/dnsmasq.sh $(1)/opt/usr/lib/keen-pbr/dnsmasq.sh
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/usr/lib/keen-pbr/dnsmasq.conf.template $(1)/opt/usr/lib/keen-pbr/dnsmasq.conf.template
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/ndm/iflayerchanged.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/iflayerchanged.d/50-keen-pbr-routing.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/ndm/ifstatechanged.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/ifstatechanged.d/50-keen-pbr-routing.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/ndm/netfilter.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/netfilter.d/50-keen-pbr-routing.sh
	$(CP) $(PKG_BUILD_DIR)/frontend/dist/. $(1)/opt/usr/share/keen-pbr/frontend/
	chmod -R a=rX,u+w $(1)/opt/usr/share/keen-pbr/frontend
	$(INSTALL_CONF) $(KEEN_PBR_FULL_CONFIG) $(1)/opt/etc/keen-pbr/config.json
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

#
# keen-pbr-headless blocks
#

define Package/keen-pbr-headless
  SECTION:=net
  CATEGORY:=Network
  TITLE:=Keen Policy-Based Routing (headless)
  VERSION:=$(PKG_VERSION)-$(PKG_RELEASE)
  DEPENDS:=+libatomic +libcurl +libnl-core +libnl-route +libstdcpp +zlib +libzstd
  VARIANT:=headless
endef

define Package/keen-pbr-headless/description
  Policy-based routing daemon for Keen/OpenWRT routers without REST API and WebUI.
endef

define Package/keen-pbr-headless/conffiles
/opt/etc/keen-pbr/config.json
/opt/etc/keen-pbr/local.lst
/opt/etc/keen-pbr/dnsmasq-fallback.conf
endef

define Package/keen-pbr-headless/install
	$(INSTALL_DIR) $(1)/opt/etc/init.d
	$(INSTALL_DIR) $(1)/opt/etc/dnsmasq.d
	$(INSTALL_DIR) $(1)/opt/etc/keen-pbr
	$(INSTALL_DIR) $(1)/opt/etc/ndm/iflayerchanged.d
	$(INSTALL_DIR) $(1)/opt/etc/ndm/ifstatechanged.d
	$(INSTALL_DIR) $(1)/opt/etc/ndm/netfilter.d
	$(INSTALL_DIR) $(1)/opt/usr/bin
	$(INSTALL_DIR) $(1)/opt/usr/lib/keen-pbr
	$(INSTALL_BIN) $(PKG_INSTALL_DIR)/$(KEEN_PBR_STAGED_BINDIR)/keen-pbr $(1)/opt/usr/bin/keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/init.d/S80keen-pbr $(1)/opt/etc/init.d/S80keen-pbr
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/defaults $(1)/opt/etc/keen-pbr/defaults
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/dnsmasq-fallback.conf $(1)/opt/etc/keen-pbr/dnsmasq-fallback.conf
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/usr/lib/keen-pbr/dnsmasq.sh $(1)/opt/usr/lib/keen-pbr/dnsmasq.sh
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/usr/lib/keen-pbr/dnsmasq.conf.template $(1)/opt/usr/lib/keen-pbr/dnsmasq.conf.template
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/ndm/iflayerchanged.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/iflayerchanged.d/50-keen-pbr-routing.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/ndm/ifstatechanged.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/ifstatechanged.d/50-keen-pbr-routing.sh
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/opt/etc/ndm/netfilter.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/netfilter.d/50-keen-pbr-routing.sh
	$(INSTALL_CONF) $(KEEN_PBR_HEADLESS_CONFIG) $(1)/opt/etc/keen-pbr/config.json
	$(INSTALL_CONF) $(PKG_BUILD_DIR)/packages/common/local.lst $(1)/opt/etc/keen-pbr/local.lst
endef

define Package/keen-pbr-headless/postinst
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/postinst-headless)
endef

define Package/keen-pbr-headless/prerm
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/prerm)
endef

define Package/keen-pbr-headless/postrm
$(file <$(PKG_BUILD_DIR)/packages/keenetic/keen-pbr/files/postrm)
endef

$(eval $(call BuildPackage,keen-pbr))
$(eval $(call BuildPackage,keen-pbr-headless))
