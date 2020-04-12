.PHONY: all supervise clean

all: supervise

supervise:
	$(MAKE) --directory=supervise
	-mkdir output
	cp supervise/supervise output/
	cp supervise/supervise.conf output/
	cp supervise/svc output/
	cp supervise/control output/
clean:
	$(MAKE) --directory=supervise clean
	-rm -rf output

