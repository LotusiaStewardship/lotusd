package=libxkbcommon
$(package)_version=0.8.4
$(package)_download_path=https://xkbcommon.org/download/
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=60ddcff932b7fd352752d51a5c4f04f3d0403230a584df9a2e0d5ed87c486c8b
$(package)_dependencies=libxcb

# Qt 5.15's xcb platform plugin needs both xkbcommon and xkbcommon-x11 through
# pkg-config; libxkbcommon ships them in this one tarball (xkbcommon-x11 is built
# when xcb-xkb is found, which is why libxcb keeps --enable-xkb). Built static
# because contrib/devtools/symbol-check.py does not allow libxkbcommon.so in
# lotus-qt's NEEDED entries.
# --disable-maintainer-mode is required: the tarball's configure.ac enables maintainer
# mode, and only with it off does automake's am__skipyacc guard ("test -f $@ ||") skip the
# YACC rule. Leaving it on makes every build regenerate the shipped src/xkbcomp/parser.c
# and fail with "yacc: command not found" (Error 127) because the image has no bison.
define $(package)_set_vars
$(package)_config_opts=--disable-shared --with-pic --disable-docs --disable-maintainer-mode
$(package)_config_opts+= --disable-dependency-tracking --enable-option-checking
# libxkbcommon enables -Werror=array-bounds, which fails to build on GCC 12+.
$(package)_cflags+=-Wno-error=array-bounds
endef

define $(package)_preprocess_cmds
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub build-aux
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) -j$(JOBS)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -f lib/*.la
endef
