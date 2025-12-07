# Department Messaging System (TCP + UDP)

This is my Computer Networks semester project.  
It is a communication system where different departments and campuses can chat using a server written in C.

### What the system includes:
- A **server** that handles all clients
- A **client** program for students/users
- An **admin tool** to see who is online and send announcements
- Authentication using Campus + Department + Password
- Messaging between departments
- Heartbeat (client sends signal every few seconds so the server knows it's active)

### How to compile:
gcc server.c -o server  
gcc client.c -o client  
gcc admin.c -o admin  

### How to run:
1. Start the server:  
   `./server`

2. Start one or more clients:  
   `./client`

3. Start admin tool:  
   `./admin`

### What I learned:
- How TCP and UDP work  
- How to handle many clients using `select()`  
- How to design a simple communication protocol  
- Basic system-level coding in C  

This project was built for my Computer Networks course (Fall 2025).
