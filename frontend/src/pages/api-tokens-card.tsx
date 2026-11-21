import { apiErrorMessage } from "@/api/client"
import { useState } from "react"
import { useTranslation } from "react-i18next"

import { useQueryClient } from "@tanstack/react-query"
import { Copy, Trash2 } from "lucide-react"
import { toast } from "sonner"

import {
  getGetAuthTokensQueryKey,
  useGetAuthTokens,
  usePostAuthTokens,
  usePostAuthTokensRevoke,
} from "@/api/generated/keen-api"
import type { ApiTokenInfo } from "@/api/generated/model/apiTokenInfo"
import type { ApiTokenScope } from "@/api/generated/model/apiTokenScope"
import { Field, FieldDescription, FieldLabel } from "@/components/shared/field"
import { ResponsiveDialog } from "@/components/shared/responsive-dialog"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { Checkbox } from "@/components/ui/checkbox"
import { Input } from "@/components/ui/input"
import { Skeleton } from "@/components/ui/skeleton"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"

const SCOPES: ApiTokenScope[] = [
  "read:prometheus",
  "read",
  "write:rules",
  "write",
]

/** Days until a new token expires; 0 means it never does. */
const EXPIRY_CHOICES = [30, 90, 365, 0] as const

const SCOPE_KEYS: Record<ApiTokenScope, string> = {
  "read:prometheus": "readPrometheus",
  read: "read",
  "write:rules": "writeRules",
  write: "write",
}

export function ApiTokensCard({ authEnabled }: { authEnabled: boolean }) {
  const { t, i18n } = useTranslation()
  const queryClient = useQueryClient()
  const tokensQuery = useGetAuthTokens()
  const createMutation = usePostAuthTokens()
  const revokeMutation = usePostAuthTokensRevoke()

  const [creating, setCreating] = useState(false)
  const [name, setName] = useState("")
  const [scopes, setScopes] = useState<ApiTokenScope[]>(["read"])
  const [expiryDays, setExpiryDays] = useState<number>(90)
  const [formError, setFormError] = useState<string | null>(null)
  const [createdToken, setCreatedToken] = useState<string | null>(null)
  // When the page was opened: enough to flag a token that has already expired.
  const [openedAt] = useState(() => Date.now())
  const [pendingRevoke, setPendingRevoke] = useState<ApiTokenInfo | null>(null)

  const tokens =
    tokensQuery.data?.status === 200 ? tokensQuery.data.data.tokens : []

  const formatTime = (seconds: number) =>
    new Date(seconds * 1000).toLocaleString(i18n.language)

  const refresh = () =>
    queryClient.invalidateQueries({ queryKey: getGetAuthTokensQueryKey() })

  // The plaintext token must not outlive its dialog: it is also kept in the
  // mutation result, so clear both.
  const closeCreated = () => {
    setCreatedToken(null)
    createMutation.reset()
  }

  const cancelCreate = () => {
    setCreating(false)
    createMutation.reset()
  }

  const openCreate = () => {
    setName("")
    setScopes(["read"])
    setExpiryDays(90)
    setFormError(null)
    setCreating(true)
  }

  const create = async () => {
    const trimmed = name.trim()
    if (!trimmed) {
      setFormError(t("auth.tokens.nameRequired"))
      return
    }
    if (scopes.length === 0) {
      setFormError(t("auth.tokens.scopeRequired"))
      return
    }
    try {
      const response = await createMutation.mutateAsync({
        data: {
          name: trimmed,
          scopes,
          ...(expiryDays > 0
            ? {
                expires_at:
                  Math.floor(Date.now() / 1000) + expiryDays * 24 * 60 * 60,
              }
            : {}),
        },
      })
      if (response.status === 200) {
        setCreating(false)
        setCreatedToken(response.data.token)
        await refresh()
      }
    } catch (error) {
      setFormError(apiErrorMessage(error, t("auth.tokens.createFailed")))
    }
  }

  const revoke = async () => {
    if (!pendingRevoke) return
    try {
      await revokeMutation.mutateAsync({ data: { id: pendingRevoke.id } })
      toast.success(t("auth.tokens.revoked"))
      setPendingRevoke(null)
      await refresh()
    } catch (error) {
      toast.error(apiErrorMessage(error, t("auth.tokens.revokeFailed")))
    }
  }

  const copyToken = async () => {
    if (!createdToken) return
    try {
      await navigator.clipboard.writeText(createdToken)
      toast.success(t("auth.tokens.copied"))
    } catch {
      toast.error(t("auth.tokens.copyFailed"))
    }
  }

  const toggleScope = (scope: ApiTokenScope, checked: boolean) =>
    setScopes((current) =>
      checked
        ? [...current.filter((item) => item !== scope), scope]
        : current.filter((item) => item !== scope)
    )

  return (
    <Card>
      <CardHeader>
        <CardTitle>{t("auth.tokens.title")}</CardTitle>
        <CardDescription>{t("auth.tokens.description")}</CardDescription>
      </CardHeader>
      <CardContent className="space-y-4">
        {!authEnabled ? (
          <Alert>
            <AlertDescription>{t("auth.tokens.needsAuth")}</AlertDescription>
          </Alert>
        ) : null}

        {tokensQuery.isLoading ? (
          <Skeleton className="h-20 w-full" />
        ) : tokensQuery.isError ? (
          <Alert variant="destructive">
            <AlertDescription>{t("auth.tokens.loadFailed")}</AlertDescription>
          </Alert>
        ) : tokens.length === 0 ? (
          <p className="text-sm text-muted-foreground">
            {t("auth.tokens.empty")}
          </p>
        ) : (
          <Table>
            <TableHeader>
              <TableRow>
                <TableHead>{t("auth.tokens.name")}</TableHead>
                <TableHead>{t("auth.tokens.scopes")}</TableHead>
                <TableHead>{t("auth.tokens.expires")}</TableHead>
                <TableHead>{t("auth.tokens.lastUsed")}</TableHead>
                <TableHead className="w-10" />
              </TableRow>
            </TableHeader>
            <TableBody>
              {tokens.map((token) => (
                <TableRow key={token.id}>
                  <TableCell className="font-medium">{token.name}</TableCell>
                  <TableCell>
                    <div className="flex flex-wrap gap-1">
                      {token.scopes.map((scope) => (
                        <Badge key={scope} variant="secondary">
                          {scope}
                        </Badge>
                      ))}
                    </div>
                  </TableCell>
                  <TableCell>
                    {token.expires_at ? (
                      <span className="inline-flex flex-wrap items-center gap-2">
                        {formatTime(token.expires_at)}
                        {token.expires_at * 1000 <= openedAt ? (
                          <Badge variant="destructive">
                            {t("auth.tokens.expired")}
                          </Badge>
                        ) : null}
                      </span>
                    ) : (
                      t("auth.tokens.never")
                    )}
                  </TableCell>
                  <TableCell>
                    {token.last_used_at
                      ? formatTime(token.last_used_at)
                      : t("auth.tokens.notUsed")}
                  </TableCell>
                  <TableCell>
                    <Button
                      aria-label={t("auth.tokens.revoke")}
                      onClick={() => setPendingRevoke(token)}
                      size="icon"
                      type="button"
                      variant="ghost"
                    >
                      <Trash2 />
                    </Button>
                  </TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        )}

        <div className="flex justify-end">
          <Button
            disabled={!authEnabled}
            onClick={openCreate}
            type="button"
            variant="outline"
          >
            {t("auth.tokens.create")}
          </Button>
        </div>
      </CardContent>

      <ResponsiveDialog
        description={t("auth.tokens.createDescription")}
        onOpenChange={(open) => !open && cancelCreate()}
        open={creating}
        title={t("auth.tokens.create")}
      >
        <div className="space-y-4">
          <Field>
            <FieldLabel htmlFor="api-token-name">
              {t("auth.tokens.name")}
            </FieldLabel>
            <Input
              id="api-token-name"
              maxLength={128}
              onChange={(event) => setName(event.target.value)}
              placeholder={t("auth.tokens.namePlaceholder")}
              value={name}
            />
          </Field>

          <div className="space-y-3">
            <FieldLabel>{t("auth.tokens.scopes")}</FieldLabel>
            {SCOPES.map((scope) => (
              <div className="flex items-start gap-3" key={scope}>
                <Checkbox
                  checked={scopes.includes(scope)}
                  id={`api-token-scope-${scope}`}
                  onCheckedChange={(value) =>
                    toggleScope(scope, value === true)
                  }
                />
                <div className="space-y-0.5">
                  <FieldLabel htmlFor={`api-token-scope-${scope}`}>
                    <code>{scope}</code>
                  </FieldLabel>
                  <FieldDescription>
                    {t(`auth.tokens.scopeHelp.${SCOPE_KEYS[scope]}`)}
                  </FieldDescription>
                </div>
              </div>
            ))}
          </div>

          <Field>
            <FieldLabel htmlFor="api-token-expiry">
              {t("auth.tokens.expires")}
            </FieldLabel>
            <select
              className="h-10 w-full rounded-lg border border-input bg-background px-3 text-sm"
              id="api-token-expiry"
              onChange={(event) => setExpiryDays(Number(event.target.value))}
              value={expiryDays}
            >
              {EXPIRY_CHOICES.map((days) => (
                <option key={days} value={days}>
                  {days === 0
                    ? t("auth.tokens.never")
                    : t("auth.tokens.expiryDays", { count: days })}
                </option>
              ))}
            </select>
          </Field>

          {formError ? (
            <Alert variant="destructive">
              <AlertDescription>{formError}</AlertDescription>
            </Alert>
          ) : null}

          <div className="flex justify-end gap-2">
            <Button onClick={cancelCreate} type="button" variant="outline">
              {t("common.cancel")}
            </Button>
            <Button
              disabled={createMutation.isPending}
              onClick={() => void create()}
              type="button"
            >
              {t("auth.tokens.create")}
            </Button>
          </div>
        </div>
      </ResponsiveDialog>

      <ResponsiveDialog
        description={t("auth.tokens.createdDescription")}
        onOpenChange={(open) => !open && closeCreated()}
        open={createdToken !== null}
        title={t("auth.tokens.createdTitle")}
      >
        <div className="space-y-4">
          <div className="flex items-center gap-2">
            <code className="min-w-0 flex-1 rounded-lg bg-muted px-3 py-2 text-xs break-all">
              {createdToken}
            </code>
            <Button
              aria-label={t("auth.tokens.copy")}
              onClick={() => void copyToken()}
              size="icon"
              type="button"
              variant="outline"
            >
              <Copy />
            </Button>
          </div>
          <div className="flex justify-end">
            <Button onClick={closeCreated} type="button">
              {t("auth.tokens.done")}
            </Button>
          </div>
        </div>
      </ResponsiveDialog>

      <ResponsiveDialog
        description={t("auth.tokens.revokeDescription")}
        onOpenChange={(open) => !open && setPendingRevoke(null)}
        open={pendingRevoke !== null}
        title={t("auth.tokens.revokeTitle", {
          name: pendingRevoke?.name ?? "",
        })}
      >
        <div className="flex justify-end gap-2">
          <Button
            onClick={() => setPendingRevoke(null)}
            type="button"
            variant="outline"
          >
            {t("common.cancel")}
          </Button>
          <Button
            disabled={revokeMutation.isPending}
            onClick={() => void revoke()}
            type="button"
            variant="destructive"
          >
            {t("auth.tokens.revoke")}
          </Button>
        </div>
      </ResponsiveDialog>
    </Card>
  )
}
