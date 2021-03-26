BEGIN TRANSACTION;
PRAGMA user_version = 1;
PRAGMA foreign_keys = ON;
CREATE TABLE files (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  url TEXT,
  path TEXT,
  mime_type TEXT,
  size INTEGER
);
CREATE TABLE media (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  file_id INTEGER NOT NULL UNIQUE,
  thumbnail_id INTEGER REFERENCES media(id),
  width INTEGER,
  height INTEGER,
  FOREIGN KEY(file_id) REFERENCES files(id)
);
CREATE TABLE users (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  username TEXT NOT NULL,
  alias TEXT,
  avatar_id INTEGER REFERENCES media(id),
  type INTEGER NOT NULL,
  UNIQUE (username, type)
);
INSERT INTO users VALUES(1,'SMS',NULL,NULL,1);
INSERT INTO users VALUES(2,'MMS',NULL,NULL,1);
CREATE TABLE accounts (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL REFERENCES users(id),
  password TEXT,
  enabled INTEGER DEFAULT 0,
  protocol INTEGER NOT NULL,
  UNIQUE (user_id, protocol)
);
INSERT INTO accounts VALUES(1,1,NULL,0,1);
INSERT INTO accounts VALUES(2,2,NULL,0,2);
CREATE TABLE threads (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  alias TEXT,
  avatar_id INTEGER REFERENCES media(id),
  account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
  type INTEGER NOT NULL,
  encrypted INTEGER DEFAULT 0,
  UNIQUE (name, account_id, type)
);
CREATE TABLE thread_members (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  thread_id INTEGER NOT NULL REFERENCES threads(id) ON DELETE CASCADE,
  user_id INTEGER NOT NULL REFERENCES users(id),
  UNIQUE (thread_id, user_id)
);
CREATE TABLE messages (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  uid TEXT NOT NULL,
  thread_id INTEGER NOT NULL REFERENCES threads(id) ON DELETE CASCADE,
  sender_id INTEGER REFERENCES users(id),
  user_alias TEXT,
  body TEXT NOT NULL,
  body_type INTEGER NOT NULL,
  direction INTEGER NOT NULL,
  time INTEGER NOT NULL,
  status INTEGER,
  encrypted INTEGER DEFAULT 0,
  UNIQUE (uid, thread_id, body, time)
);
ALTER TABLE threads ADD COLUMN last_read_id INTEGER REFERENCES messages(id);

INSERT INTO users VALUES(3,'user@example.com',NULL,NULL,3);
INSERT INTO users VALUES(4,'buddy@example.com',NULL,NULL,3);
INSERT INTO users VALUES(5,'friend@example.com',NULL,NULL,3);
INSERT INTO users VALUES(6,'bob@example.com',NULL,NULL,3);
INSERT INTO users VALUES(7,'account@example.com',NULL,NULL,3);
INSERT INTO users VALUES(8,'alice@example.com',NULL,NULL,3);

INSERT INTO accounts VALUES(3,7,NULL,0,3);
INSERT INTO accounts VALUES(4,8,NULL,0,3);

INSERT INTO threads VALUES(1,'bob@example.com',NULL,NULL,3,0,0,NULL);
INSERT INTO threads VALUES(2,'friend@example.com',NULL,NULL,3,0,0,NULL);
INSERT INTO threads VALUES(3,'user@example.com',NULL,NULL,3,0,0,NULL);
INSERT INTO threads VALUES(4,'buddy@example.com',NULL,NULL,3,0,0,NULL);
INSERT INTO threads VALUES(5,'bob@example.com',NULL,NULL,4,0,0,NULL);

INSERT INTO thread_members VALUES(1,1,6);
INSERT INTO thread_members VALUES(2,2,5);
INSERT INTO thread_members VALUES(3,3,3);
INSERT INTO thread_members VALUES(4,4,4);
INSERT INTO thread_members VALUES(5,5,6);

INSERT INTO messages VALUES(NULL,'2ebff02a-0d1b-11eb-aa37-5fdd4a70e5d0',1,6,NULL,'Hi',2,-1,1602143867,NULL,0);
INSERT INTO messages VALUES(NULL,'NKkdrK32DFDsXDUZl',2,5,NULL,'Message with resource',2,1,1602143838,NULL,0);
INSERT INTO messages VALUES(NULL,'NKkdrKrNSDsXDUZl',3,3,NULL,'Another test message',2,-1,1602143858,NULL,0);
INSERT INTO messages VALUES(NULL,'NKkdrKrNSDsXsdxZl',3,3,NULL,'This is a system message',2,0,1602143858,NULL,0);
INSERT INTO messages VALUES(NULL,'NKkdrKrNSbYlZUZl',4,4,NULL,'Some test message',2,1,1602158858,NULL,0);
INSERT INTO messages VALUES(NULL,'4b58bb22-0d1b-11eb-b502-8b03cec4d745',5,6,NULL,'Hi',2,-1,1602145677,NULL,0);
INSERT INTO messages VALUES(NULL,'e465e9da-0d1a-11eb-93ea-e30b7b9ae820',5,6,NULL,'Hi',2,1,1602143859,NULL,0);

COMMIT;
