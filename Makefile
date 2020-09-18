TARGETS := all debug install clean
PROGRAMS := dns-dnat dyndnat nfq-unit-start resolve-hostsfile

$(TARGETS): $(PROGRAMS)

$(PROGRAMS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: $(TARGETS) $(PROGRAMS)
