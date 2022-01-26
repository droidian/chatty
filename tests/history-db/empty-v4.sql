BEGIN TRANSACTION;
PRAGMA user_version = 4;
PRAGMA foreign_keys = ON;
CREATE TABLE mime_type (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL UNIQUE
);
CREATE TABLE files (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  name TEXT,
  url TEXT NOT NULL UNIQUE,
  path TEXT,
  mime_type_id INTEGER REFERENCES mime_type(id),
  status INT,
  size INTEGER
);
CREATE TABLE file_metadata (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  file_id INTEGER NOT NULL UNIQUE REFERENCES files(id) ON DELETE CASCADE,
  width INTEGER,
  height INTEGER,
  duration INTEGER,
  FOREIGN KEY(file_id) REFERENCES files(id)
);
CREATE TABLE users (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  username TEXT NOT NULL,
  alias TEXT,
  avatar_id INTEGER REFERENCES files(id),
  type INTEGER NOT NULL,
  UNIQUE (username, type)
);
INSERT INTO users VALUES(1,'invalid-0000000000000000',NULL,NULL,1);
CREATE TABLE accounts (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  user_id INTEGER NOT NULL REFERENCES users(id),
  password TEXT,
  enabled INTEGER DEFAULT 0,
  protocol INTEGER NOT NULL,
  UNIQUE (user_id, protocol)
);
INSERT INTO accounts VALUES(1,1,NULL,0,1);
CREATE TABLE threads (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  alias TEXT,
  avatar_id INTEGER REFERENCES files(id),
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
  preview_id INTEGER REFERENCES files(id),
  subject TEXT,
  UNIQUE (uid, thread_id, body, time)
);
CREATE TABLE mm_messages (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  message_id INTEGER NOT NULL UNIQUE REFERENCES messages(id) ON DELETE CASCADE,
  account_id INTEGER NOT NULL REFERENCES accounts(id),
  protocol INTEGER NOT NULL,
  smsc TEXT,
  time_sent INTEGER,
  validity INTEGER,
  reference_number INTEGER
);
CREATE TABLE message_files (
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  message_id INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
  file_id INTEGER NOT NULL REFERENCES files(id),
  preview_id INTEGER REFERENCES files(id),
  UNIQUE (message_id, file_id)
);
ALTER TABLE threads ADD COLUMN last_read_id INTEGER REFERENCES messages(id);
ALTER TABLE threads ADD COLUMN visibility INT NOT NULL DEFAULT 0;
ALTER TABLE threads ADD COLUMN notification INTEGER NOT NULL DEFAULT 1;

COMMIT;
