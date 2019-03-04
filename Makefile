shell: wilsodav.shell.c
	gcc wilsodav.shell.c -o smallsh  

clean:
	rm -f smallsh  junk*
	rm -rf testdir*