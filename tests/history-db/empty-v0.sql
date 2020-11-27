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

COMMIT;
