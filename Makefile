all:
	@clang++ trustc.cpp -o trustc -std=c++17
	@echo "trustc building success. Running test code"
	@./trustc main.trust
	@./main

run: 
	@./trustc main.trust
	@./main