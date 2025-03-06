all:
	@clang++ trustc.cpp -o trustc -std=c++17
	@./trustc main.trust
	@./main

run: 
	@./trustc main.trust
	@./main