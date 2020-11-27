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
CREATE TABLE users (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  username TEXT NOT NULL,
  alias TEXT, avatar_id INTEGER REFERENCES files(id),
  thumbnail_id INTEGER REFERENCES files(id),
  type INTEGER NOT NULL,
  UNIQUE (username, type)
);
INSERT INTO users VALUES(1,'SMS',NULL,NULL,NULL,1);
INSERT INTO users VALUES(2,'MMS',NULL,NULL,NULL,1);
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
CREATE TABLE media (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  file_id INTEGER NOT NULL UNIQUE,
  thumbnail_id INTEGER REFERENCES media(id),
  width INTEGER,
  height INTEGER,
  FOREIGN KEY(file_id) REFERENCES files(id)
);
ALTER TABLE threads ADD COLUMN last_read_id INTEGER REFERENCES messages(id);

COMMIT;
