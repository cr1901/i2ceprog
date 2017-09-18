CFLAGS=-g
CPPFLAGS=
LDFLAGS=
LIBS=
RM=rm
RMFLAGS=-f
INSTALL=install


i2ceprog: i2ceprog.c 
#	$(CC) $(LDFLAGS) $(LIBPATH) -o $@ $(GPIOBJS) $(LIBS)

hello: hello.c

.c:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(LIBPATH) -o $@ $< $(LIBS)

.c.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

clean:
	$(RM) $(RMFLAGS) *.o *.s urand_digits i2ceprog hello

test: i2ceprog
	dd if=/dev/urandom of=urand_digits bs=512 count=1
	su root -c './i2ceprog urand_digits'
	

