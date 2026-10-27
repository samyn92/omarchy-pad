# Generates the Wayland protocol glue with wayland-scanner, shared by the app
# and the tests.
STAGING = /usr/share/wayland-protocols/staging
WAYLAND_PROTOCOLS = \
    $$STAGING/ext-image-copy-capture/ext-image-copy-capture-v1.xml \
    $$STAGING/ext-image-capture-source/ext-image-capture-source-v1.xml \
    $$STAGING/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml \
    $$PWD/src/protocols/hyprland-toplevel-mapping-v1.xml

wayland_header.input = WAYLAND_PROTOCOLS
wayland_header.output = ${QMAKE_FILE_BASE}-client-protocol.h
wayland_header.commands = wayland-scanner client-header ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
wayland_header.variable_out = HEADERS
wayland_header.CONFIG += target_predeps no_link
QMAKE_EXTRA_COMPILERS += wayland_header

wayland_code.input = WAYLAND_PROTOCOLS
wayland_code.output = ${QMAKE_FILE_BASE}-protocol.c
wayland_code.commands = wayland-scanner private-code ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
wayland_code.variable_out = SOURCES
wayland_code.depends = ${QMAKE_FILE_BASE}-client-protocol.h
QMAKE_EXTRA_COMPILERS += wayland_code

INCLUDEPATH += $$OUT_PWD
