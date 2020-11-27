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

INSERT INTO chatty_chat VALUES(NULL,1587854453,1,'alice@example.org','room@conference.example.com','room@conference.example.com/bob','1fa48654-0eed-11eb-9110-b7542262f3bf','Hello everyone');
INSERT INTO chatty_chat VALUES(NULL,1587854455,1,'alice@example.org','room@conference.example.com','room@conference.example.com/bob','40a341d8-0eed-11eb-91be-dbcbdfd6fab6','Good morning');
INSERT INTO chatty_chat VALUES(NULL,1587854458,1,'alice@example.org','room@conference.example.com','bob@example.com','96001322-0eed-11eb-b943-ffb19c0eb13a','Hi');
INSERT INTO chatty_chat VALUES(NULL,1587854459,1,'alice@example.org','room@conference.example.com',NULL,'1587644391694312','Is this good?');
INSERT INTO chatty_chat VALUES(NULL,1587854658,1,'jhon@example.org','another-room@conference.example.com','bob@example.com/MY7yLBdJ','12c97d94-0eee-11eb-86e0-7fe0e99a74bb','Is this another room?');
INSERT INTO chatty_chat VALUES(NULL,1587854658,-1,'jhon@example.org','another-room@conference.example.com',NULL,'43511f76-0eee-11eb-98fc-23b32f642943','Yes this is another room');
INSERT INTO chatty_chat VALUES(NULL,1587854661,-1,'jhon@example.org','another-room@conference.example.com','jhon@example.org','7f21eca6-0eee-11eb-bdfd-5be4cafcdd69','Feel free to speak anything');
INSERT INTO chatty_chat VALUES(NULL,1587854682,-1,'charlie@example.org','room@conference.example.com',NULL,'d4097d22-0efa-11eb-b349-9317bde881f6','Hello');

COMMIT;
