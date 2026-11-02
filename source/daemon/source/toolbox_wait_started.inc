/* A ShellUI that has not loaded its trophy modules yet (a send right after
   the restart that reloading the stack makes) gets the toolbox once they
   are in and the resume delay has passed, as after rest mode; kstuff's own
   ShellUI patch needs those modules too. They load within ~11 s of the
   restart (11.40), so this is no guard for 13.42, which froze on an
   injection 25-32 s after it: the host tools still wait 75 s there. A
   ShellUI that is up is not kept waiting. */
void toolbox_wait_started(pid_t pid) {
  bool starting = false;
  for (const char *name : kShellUiReadySprx)
    starting = starting || !toolbox_sprx_loaded(pid, name);
  if (!starting)
    return;
  const uint32_t gen = g_rest_gen.load(std::memory_order_acquire);
  LOG_INFO("SceShellUI pid=%d is still starting; waiting for it",
           static_cast<int>(pid));
  if (toolbox_wait_shellui_sprx(pid, gen))
    toolbox_wait_resume_delay(pid, gen);
}
