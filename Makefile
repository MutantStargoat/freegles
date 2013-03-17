.PHONY: all
all:
	$(MAKE) -C src/egl

.PHONY: clean
clean:
	$(MAKE) clean -C src/egl

.PHONY: install
install:
	$(MAKE) install -C src/egl

.PHONY: uninstall
uninstall:
	$(MAKE) uninstall -C src/egl
