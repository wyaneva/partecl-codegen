# Configuration parser

This file contains some specific information on how the configuration parser generates data structures from the configuration file.

## Test case inputs

Test case inputs are parsed into the following structure:

```
static struct Declaration {
  std::string type;
  std::string name;
  bool isArray;
  bool isConst;
  bool isPointer;
  int size; // set to -1 if not array
} Declaration;
```

## Structure generation

The table below provides a high level overview on how the configuration options are mapped into memory structures:

| Configuration input | Variable | Comments |
| ------------------- | -------- | --------
| **Simple data type**<br>Example:<br>`int a` | **Simple data type**<br><br>`int a` | |
| **Array**<br>Example:<br>`int a[10]` | **Array**<br><br>`int a[10]` | The tool assumes that individual array values are <br> passed as **separate** columns in the CSV file with test cases. |
| **Pointer**<br>Example:<br>`char* astr` | **Array**<br><br>`char astr[500]` | Pointers are turned into arrays of size 500 (hard-coded value for now).<br>The tool assumes that the values are passed <br> as a **single** string value in the CSV file with test cases. |
| **Stdin**<br>Example:<br>`char *stdin1` | **char[500]**<br><br>`char stdin1[500]` | Similar to normal pointers, the tool turns them into arrays of size 500.<br>It assumes that they are passed as a **single** string value<br>in the CSV file with test cases.  To differentiate from pointers, the value<br>needs to be AFTER the 'less than' (`<`) character. |
