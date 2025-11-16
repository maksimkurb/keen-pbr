_clean:
	rm -rf out/$(BUILD_DIR)
	mkdir -p out/$(BUILD_DIR)/control
	mkdir -p out/$(BUILD_DIR)/data

_conffiles:
	cp common/ipk/conffiles out/$(BUILD_DIR)/control/conffiles

_control:
	echo "Package: keen-pbr" > out/$(BUILD_DIR)/control/control
	echo "Version: $(VERSION)" >> out/$(BUILD_DIR)/control/control
	echo "Depends: dnsmasq-full, ipset, iptables, cron" >> out/$(BUILD_DIR)/control/control
	echo "Conflicts: keenetic-pbr" >> out/$(BUILD_DIR)/control/control
	echo "License: MIT" >> out/$(BUILD_DIR)/control/control
	echo "Section: net" >> out/$(BUILD_DIR)/control/control
	echo "URL: https://github.com/maksimkurb/keen-pbr" >> out/$(BUILD_DIR)/control/control
	echo "Architecture: $(ARCH)" >> out/$(BUILD_DIR)/control/control
	echo "Description:  Policy-based routing toolkit for Keenetic routers" >> out/$(BUILD_DIR)/control/control
	echo "" >> out/$(BUILD_DIR)/control/control

_scripts:
	cp common/ipk/postinst out/$(BUILD_DIR)/control/postinst
	cp common/ipk/postrm out/$(BUILD_DIR)/control/postrm

_binary:
	GOOS=linux GOARCH=$(GOARCH) go build -o out/$(BUILD_DIR)/data$(ROOT_DIR)/usr/bin/keen-pbr -ldflags "-w -s" ./src/cmd/keen-pbr

_pkgfiles:
	cat etc/init.d/S80keen-pbr > out/$(BUILD_DIR)/data$(ROOT_DIR)/etc/init.d/S80keen-pbr; \
	chmod +x out/$(BUILD_DIR)/data$(ROOT_DIR)/etc/init.d/S80keen-pbr; \

	cp -r etc/cron.daily out/$(BUILD_DIR)/data$(ROOT_DIR)/etc/cron.daily;

	cp -r etc/dnsmasq.d out/$(BUILD_DIR)/data$(ROOT_DIR)/etc/dnsmasq.d;
	cp etc/dnsmasq.conf.keen-pbr out/$(BUILD_DIR)/data$(ROOT_DIR)/etc/dnsmasq.conf.keen-pbr;

_ipk:
	make _clean

	# control.tar.gz
	make _conffiles
	make _control
	make _scripts
	cd out/$(BUILD_DIR)/control; tar czvf ../control.tar.gz .; cd ../../..

	# data.tar.gz
	mkdir -p out/$(BUILD_DIR)/data$(ROOT_DIR)/etc/init.d

	cp -r etc/keen-pbr out/$(BUILD_DIR)/data$(ROOT_DIR)/etc/keen-pbr
	make _pkgfiles

	cp -r etc/ndm out/$(BUILD_DIR)/data$(ROOT_DIR)/etc/ndm; \

	make _binary; \

	cd out/$(BUILD_DIR)/data; tar czvf ../data.tar.gz .; cd ../../..

	# ipk
	echo 2.0 > out/$(BUILD_DIR)/debian-binary
	cd out/$(BUILD_DIR); \
	tar czvf ../$(FILENAME) control.tar.gz data.tar.gz debian-binary; \
	cd ../..

mipsel:
	@make \
		BUILD_DIR=mipsel \
		ARCH=mipsel-3.4 \
		GOARCH=mipsle \
		FILENAME=keen-pbr_$(VERSION)_mipsel-3.4.ipk \
		BIN=mips32r1-lsb \
		_ipk

mips:
	@make \
		BUILD_DIR=mips \
		ARCH=mips-3.4 \
		GOARCH=mips \
		FILENAME=keen-pbr_$(VERSION)_mips-3.4.ipk \
		BIN=mips32r1-msb \
		_ipk

aarch64:
	@make \
		BUILD_DIR=aarch64 \
		ARCH=aarch64-3.10 \
		GOARCH=arm64 \
		FILENAME=keen-pbr_$(VERSION)_aarch64-3.10.ipk \
		BIN=aarch64 \
		_ipk

entware: mipsel mips aarch64