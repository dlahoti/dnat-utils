TARGETS := all debug install clean
PROGRAMS := dyndnat nfq-unit-start resolve-hostfile

$(TARGETS): $(PROGRAMS)

$(PROGRAMS):
	$(MAKE) -C $@ $(MAKECMDGOALS)

.PHONY: $(TARGETS) $(PROGRAMS)
