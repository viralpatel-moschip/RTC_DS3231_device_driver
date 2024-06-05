CC = gcc

# Target executable path
TARGET = rtc_test_app

# Build the target executable
all : $(TARGET) 

# Compile source file to create executable.  
$(TARGET):rtc_test_app.c
	@$(CC) -o $@ $<

#Clean files which is generated.	
clean:
	@rm -rf rtc_test_app
