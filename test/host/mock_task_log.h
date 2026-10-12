#ifndef MOCK_TASK_LOG_H
#define MOCK_TASK_LOG_H

void mock_task_log_reset(void);
int mock_task_log_calls(void);
const char *mock_task_log_last_action(void);
const char *mock_task_log_last_reply(void);

#endif // MOCK_TASK_LOG_H
