main: rsslVA

rsslVA:
	make -f makefileVA

stubs:
	make -f makefileStubs
	
clean:
	make -f makefileVA clean
	
cleanall:
	make -f makefileVA clean
	make -f makefileStubs clean
	
all:
	make stubs
	make rsslVA