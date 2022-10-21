container_migration:container_migration.o
	gcc -lnuma container_migration.o -o container_migration
container_migration.o:container_migration.c
	gcc -lnuma -c container_migration.c
