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

INSERT INTO users VALUES(3,'alice',NULL,NULL,4);
INSERT INTO users VALUES(4,'@charlie:example.com',NULL,NULL,4);
INSERT INTO users VALUES(5,'@_freenode_hunter2:example.com',NULL,NULL,4);
INSERT INTO users VALUES(7,'@bob:example.com',NULL,NULL,4);
INSERT INTO users VALUES(8,'@bob:example.org',NULL,NULL,4);
INSERT INTO users VALUES(9,'@alice:example.com',NULL,NULL,4);

INSERT INTO accounts VALUES(3,3,NULL,0,4);
INSERT INTO accounts VALUES(4,8,NULL,0,4);
INSERT INTO accounts VALUES(5,9,NULL,0,4);

INSERT INTO threads VALUES(1,'!CDFTfyJgtVMvsXDEi:example.com','#something',NULL,4,1,0,NULL,0,1);
INSERT INTO threads VALUES(2,'!CDFTfyJgtVMvsXDEi:example.com',NULL,NULL,5,1,0,NULL,1,1);
INSERT INTO threads VALUES(3,'!VPWUCfyJyeVMxiHYGi:example.com','Some room',NULL,5,1,1,NULL,1,1);
INSERT INTO threads VALUES(4,'!VPWUCfyJyeVMxiHYGi:example.com',NULL,NULL,3,1,1,NULL,0,1);

INSERT INTO thread_members VALUES(1,1,4);
INSERT INTO thread_members VALUES(2,1,9);
INSERT INTO thread_members VALUES(3,2,5);
INSERT INTO thread_members VALUES(4,3,7);
INSERT INTO thread_members VALUES(5,3,9);
INSERT INTO thread_members VALUES(6,4,9);

INSERT INTO messages VALUES(1,'10600c18',1,4,NULL,'',11,1,1586447320,NULL,0,NULL,NULL);
INSERT INTO messages VALUES(2,'1dc29876',1,4,NULL,'',9,1,1586448432,NULL,0,NULL,NULL);
INSERT INTO messages VALUES(3,'c73bbcbc',1,9,NULL,'',10,1,1586448429,NULL,0,NULL,NULL);
INSERT INTO messages VALUES(5,'414d35fa',2,5,NULL,'',8,1,1586448435,NULL,0,NULL,NULL);
INSERT INTO messages VALUES(6,'f86768a5',2,NULL,NULL,'',9,1,1586448438,NULL,0,NULL,NULL);
INSERT INTO messages VALUES(7,'12107bfc',3,7,NULL,'',8,1,1586447316,NULL,0,NULL,NULL);
INSERT INTO messages VALUES(9,'2a5f6c4a',3,7,NULL,'',8,1,1586447319,NULL,0,NULL,NULL);
INSERT INTO messages VALUES(10,'6b67fa36-0f91-11eb',3,9,NULL,'',11,-1,1586447419,NULL,0,NULL,NULL);
INSERT INTO messages VALUES(11,'3a383ec7-7566-457b-b561-2145b328459c',4,9,NULL,'',9,-1,1586447421,NULL,0,NULL,NULL);

INSERT INTO mime_type VALUES(1,'audio/ogg');
INSERT INTO mime_type VALUES(2,'application/pdf');
INSERT INTO mime_type VALUES(3,'image/jpg');
INSERT INTO mime_type VALUES(4,'video/ogv');
INSERT INTO mime_type VALUES(5,'image/png');

INSERT INTO files VALUES(1,'document.pdf','https://example.com/document.pdf',NULL,NULL,0,0);
INSERT INTO files VALUES(2,'image.png','http://example.com/image.png','some/path/image.png',5,1,200);
INSERT INTO files VALUES(3,'another.pdf','http://example.com/another.pdf','another/path/another.pdf',2,1,400);
INSERT INTO files VALUES(4,'അ.ogv','http://example.com/അ.ogv',NULL,2,2,512);
INSERT INTO files VALUES(5,'another-image.jpg','http://example.net/another-image.jpg',NULL,3,2,512);
INSERT INTO files VALUES(6,NULL,'https://example.com/another-document.pdf',NULL,NULL,NULL,NULL);
INSERT INTO files VALUES(8,NULL,'https://example.com/song.ogg',NULL,1,NULL,NULL);
INSERT INTO files VALUES(9,'another.ogg','https://example.com/another.ogg',NULL,1,NULL,NULL);
INSERT INTO files VALUES(10,'File title','http://example.com/file.png','some/path/file.png',5,NULL,NULL);

INSERT INTO message_files VALUES(NULL,1,8,NULL);
INSERT INTO message_files VALUES(NULL,2,2,NULL);
INSERT INTO message_files VALUES(NULL,3,4,NULL);
INSERT INTO message_files VALUES(NULL,5,1,NULL);
INSERT INTO message_files VALUES(NULL,6,5,NULL);
INSERT INTO message_files VALUES(NULL,7,3,NULL);
INSERT INTO message_files VALUES(NULL,9,6,NULL);
INSERT INTO message_files VALUES(NULL,11,10,NULL);
INSERT INTO message_files VALUES(NULL,10,9,NULL);

COMMIT;
