BEGIN TRANSACTION;
CREATE TABLE chatty_im(
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  timestamp INTEGER NOT_NULL,
  direction INTEGER NOT NULL,
  account TEXT NOT_NULL,
  who TEXT NOT_NULL,
  uid TEXT NOT_NULL,
  message TEXT,
  UNIQUE (timestamp, message)
);
CREATE TABLE chatty_chat(
  id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
  timestamp INTEGER NOT_NULL,
  direction INTEGER NOT NULL,
  account TEXT NOT NULL,
  room TEXT NOT_NULL,
  who TEXT,
  uid TEXT NOT_NULL,
  message TEXT,
  UNIQUE (timestamp, message)
);
CREATE UNIQUE INDEX chatty_im_acc_uid ON chatty_im(account, uid);
CREATE UNIQUE INDEX chatty_chat_room_uid ON chatty_chat(room, uid);

INSERT INTO chatty_im VALUES(NULL,1602158858,1,'account@example.com','buddy@example.com','NKkdrKrNSbYlZUZl','Some test message');
INSERT INTO chatty_im VALUES(NULL,1602143858,-1,'account@example.com','user@example.com','NKkdrKrNSDsXDUZl','Another test message');
INSERT INTO chatty_im VALUES(NULL,1602143838,1,'account@example.com','friend@example.com/resource','NKkdrK32DFDsXDUZl','Message with resource');
INSERT INTO chatty_im VALUES(NULL,1602143858,0,'account@example.com','user@example.com','NKkdrKrNSDsXsdxZl','This is a system message');
INSERT INTO chatty_im VALUES(NULL,1602143859,1,'alice@example.com','bob@example.com','e465e9da-0d1a-11eb-93ea-e30b7b9ae820','Hi');
INSERT INTO chatty_im VALUES(NULL,1602143867,-1,'account@example.com','bob@example.com','2ebff02a-0d1b-11eb-aa37-5fdd4a70e5d0','Hi');
INSERT INTO chatty_im VALUES(NULL,1602145677,-1,'alice@example.com','bob@example.com','4b58bb22-0d1b-11eb-b502-8b03cec4d745','Hi');

COMMIT;
