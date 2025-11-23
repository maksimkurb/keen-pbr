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
	EXTRA_DEPENDS:=ipset, iptables, cron
	TITLE:=Tool for downloading, parsing and importing domains/ip/cidr lists into ipset
	URL:=https://github.com/maksimkurb/keen-pbr/
	MAINTAINER:=Maxim Kurbatov <maxim@kurb.me>
	VERSION:=$(PKG_VERSION)-$(BOARD)-$(PKG_RELEASE)
endef

define Package/keen-pbr/description
	Policy-based routing toolkit for Keenetic routers
endef

GO_TARGET:=./src/cmd/keen-pbr

# Set build-time variables
PKG_BUILD_DATE:=$(shell date -u +%Y-%m-%d)
# Use PKG_SOURCE_VERSION which contains the git commit from the source checkout
# If PKG_SOURCE_VERSION is not set (local builds), try git rev-parse
PKG_COMMIT:=$(if $(PKG_SOURCE_VERSION),$(shell echo $(PKG_SOURCE_VERSION) | cut -c1-7),$(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown"))

GO_LDFLAGS += -X 'github.com/maksimkurb/keen-pbr/src/internal/api.Version=$(PKG_VERSION)' \
	-X 'github.com/maksimkurb/keen-pbr/src/internal/api.Date=$(PKG_BUILD_DATE)' \
	-X 'github.com/maksimkurb/keen-pbr/src/internal/api.Commit=$(PKG_COMMIT)'

define Build/Compile
	# Build frontend
	cd $(PKG_BUILD_DIR)/src/frontend && \
		npm install && \
		npm run build

	# Build Go binary (frontend is served from filesystem, not embedded)
	$(call Build/Compile/Go)
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
		$(INSTALL_DIR) $(1)/opt/usr/bin/
		$(INSTALL_DIR) $(1)/opt/usr/share/keen-pbr/ui/

		echo "> INSTALL DIR = $(PKG_BUILD_DIR)"
		ls -la $(PKG_BUILD_DIR)

		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/cron.daily/50-keen-pbr-lists-update.sh $(1)/opt/etc/cron.daily/
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/ndm/ifstatechanged.d/50-keen-pbr-routing.sh $(1)/opt/etc/ndm/ifstatechanged.d/
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/ndm/netfilter.d/50-keen-pbr-fwmarks.sh $(1)/opt/etc/ndm/netfilter.d/
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/init.d/S80keen-pbr $(1)/opt/etc/init.d
		$(INSTALL_CONF) $(PKG_BUILD_DIR)/package/etc/keen-pbr/keen-pbr.conf $(1)/opt/etc/keen-pbr/
		$(INSTALL_CONF) $(PKG_BUILD_DIR)/package/etc/keen-pbr/local.lst $(1)/opt/etc/keen-pbr/
		$(INSTALL_BIN) $(PKG_BUILD_DIR)/package/etc/keen-pbr/defaults $(1)/opt/etc/keen-pbr/
		$(INSTALL_BIN) $(PKG_INSTALL_DIR)/bin/keen-pbr $(1)/opt/usr/bin/

		# Install frontend UI files
		cp -r $(PKG_BUILD_DIR)/src/frontend/dist/* $(1)/opt/usr/share/keen-pbr/ui/
endef

define Package/keen-pbr/postinst
$(file <$(PKG_BUILD_DIR)/package/entware/keen-pbr/postinst)
endef

define Package/keen-pbr/postrm
$(file <$(PKG_BUILD_DIR)/package/entware/keen-pbr/postrm)
endef

$(eval $(call BuildPackage,keen-pbr))
