all:
	echo "Choose one of container, non-root-container, network-setup, network-teardown"

container: container.c
	clang $^ -o container

non-root-container: container.c
	sudo clang $^ -o non-root-container
	sudo chmod 4755 non-root-container

network-setup:
	sudo bash networking/setup.sh

network-teardown:
	sudo bash networking/teardown.sh

test: fork_test mem_test

fork_test: fork_test.c
	clang $^ -o fork_test

mem_test: mem_test.c
	clang $^ -o mem_test
