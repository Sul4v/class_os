Here are the essential details for Project Phase 2, presented in Markdown.

## Project Phase 2
[cite_start]**Due Date:** 30 October 2025 [cite: 1]

---

### Objective
[cite_start]Upgrade the local CLI shell from Phase 1 to support remote access capabilities using socket communication[cite: 12].

---

### Flow of Operations
1.  [cite_start]**Client Initialization:** The client program starts and establishes a socket connection with the server[cite: 15].
2.  [cite_start]**User Input:** The client presents a prompt (`$`) and the user enters a command[cite: 18].
3.  [cite_start]**Command Transmission:** The client sends the command to the server via the socket[cite: 21].
4.  [cite_start]**Server Processing:** The server receives the command and executes it using the Phase 1 shell implementation[cite: 23, 25].
5.  [cite_start]**Result Transmission:** The server sends the command's output back to the client[cite: 27].
6.  [cite_start]**Client Display:** The client receives and displays the output to the user[cite: 30, 31].
7.  [cite_start]**Loop:** The process repeats from step 2, allowing for more commands[cite: 33].

---

### Output Format
* [cite_start]**Client Output:** Should look like a regular shell, just as in Phase 1[cite: 35].
* [cite_start]**Server Output:** Must demonstrate the flow of socket communication[cite: 35]. The format should be as follows:
    * [cite_start]`[INFO] Server started, waiting for client connections...` [cite: 36]
    * [cite_start]`[INFO] Client connected.` [cite: 37]
    * [cite_start]`[RECEIVED] Received command: "ls -l" from client.` [cite: 38]
    * [cite_start]`[EXECUTING] Executing command: "ls -l"` [cite: 39]
    * [cite_start]`[OUTPUT] Sending output to client:` [cite: 40]
    * (Command output, e.g., `total 12...`) [cite_start][cite: 41, 43, 45]
    * [cite_start]`[RECEIVED] Received command: "unknowncmd" from client.` [cite: 46]
    * [cite_start]`[EXECUTING] Executing command: "unknowncmd"` [cite: 47]
    * [cite_start]`[ERROR] Command not found: "unknowncmd"` [cite: 48]
    * [cite_start]`[OUTPUT] Sending error message to client: "Command not found: unknowncmd"` [cite: 49]

---

### Report Guidelines
[cite_start]Your report must include the following sections[cite: 63]:

| Section | Description |
| :--- | :--- |
| **Title Page** | Includes the phase number, group member names, and NetIDs. |
| **Architecture and Design** | Describes the high-level structure, key design decisions, reasons for using specific algorithms/data structures, file structure, and code organization. |
| **Implementation Highlights** | Explains core functionalities, important functions, algorithms, and logic. Includes references to critical code snippets. Also describes how errors and edge cases were handled. |
| **Execution Instructions** | Provides instructions on how to compile and run the program. |
| **Testing** | Lists the test cases performed. Includes screenshots and text explanations detailing the tests conducted and the output received. |
| **Challenges** | Describes any difficulties and challenges encountered and explains how they were resolved. |
| **Division of Tasks** | Clarify how the tasks/responsibilities were divided across both team members. |
| **References** | Cites any resources used during development. |

---

### Grading Rubric (Total 30 Points)
[cite_start][cite: 65]

| Description | Points |
| :--- | :--- |
| Successful compilation with a Makefile on remote Linux Server | 1 |
| Socket Implementation | 17 |
| Server output as shown in the given figures | 5 |
| Error Handling | 2 |
| Detailed Comments | 2 |
| Code modularity, quality, efficiency and organization | 1.5 |
| Report | 1.5 |

---

### Noteworthy Points
* [cite_start]**Cumulative Functionality:** All functionality from previous phases must persist and remain correct[cite: 67].
* **Address Feedback:** Feedback from previous phases must be addressed. [cite_start]Unresolved issues will lead to repeated deductions[cite: 68].
* [cite_start]**Best Practices:** Use separate compilation[cite: 72]. [cite_start]Aim for high efficiency, reduction of redundant code, and scalability[cite: 73].
* **Commenting:** Extensive, meaningful, and detailed comments explaining all code logic are required. [cite_start]Avoid short comments before large code blocks[cite: 74, 75].
* [cite_start]**Server Requirement:** Your submission must work successfully on the remote Linux server[cite: 76].

---

### Submission
[cite_start]Submit a `.zip` file containing[cite: 78]:
* [cite_start]C files + Make file [cite: 80]
* [cite_start]Report (Must be in PDF format) [cite: 81]