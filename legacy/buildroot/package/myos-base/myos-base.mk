MYOS_BASE_VERSION = 0.1
MYOS_BASE_SITE = $(BR2_EXTERNAL_MYOS_PATH)/package/myos-base/src
MYOS_BASE_SITE_METHOD = local

define MYOS_BASE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/myos-info $(TARGET_DIR)/usr/bin/myos-info
endef

define MYOS_BASE_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(@D)/S99myos $(TARGET_DIR)/etc/init.d/S99myos
endef

$(eval $(generic-package))
